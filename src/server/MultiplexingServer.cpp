#include <iostream>
#include <csignal>
#include <chrono>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <system_error>
#include <vector>

#include <logging/Logger.h>
#include <server/Metrics.h>
#include <server/MultiplexingServer.h>

// Initialize static member
MultiplexingServer *MultiplexingServer::global_instance_ = nullptr;

void MultiplexingServer::handleSignal(int signal)
{
	Logger::info("Received signal: {}", signal);
	if (global_instance_)
	{
		global_instance_->stop();
	}
}

// ClientConnection implementation
MultiplexingServer::ClientConnection::ClientConnection(int fd, const std::string &client_addr,
													   RequestHandler *request_handler,
													   const ServerConfig &config,
													   MultiplexingServer *server)
	: fd_(fd), client_addr_(client_addr), last_activity_(time(nullptr)),
	  connection_start_time_(time(nullptr)), request_handler_(request_handler),
	  config_(config), server_(server)
{
	// Make socket non-blocking
	int flags = fcntl(fd_, F_GETFL, 0);
	if (flags != -1)
	{
		fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
	}
}

MultiplexingServer::ClientConnection::~ClientConnection()
{
	close();
}

void MultiplexingServer::ClientConnection::reset(int fd, const std::string &client_addr, RequestHandler *request_handler)
{
	fd_ = fd;
	client_addr_ = client_addr;
	read_buffer_.clear();
	{
		std::lock_guard<std::mutex> lock(write_mutex_);
		write_buffer_.clear();
	}
	last_activity_ = time(nullptr);
	connection_start_time_ = time(nullptr);
	request_handler_ = request_handler;
	active_ = true;

	// Make socket non-blocking
	int flags = fcntl(fd_, F_GETFL, 0);
	if (flags != -1)
	{
		fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
	}
}

bool MultiplexingServer::ClientConnection::readAvailable()
{
	char buffer[4096];
	ssize_t bytes_read;

	// Single read attempt
	bytes_read = recv(fd_, buffer, sizeof(buffer), MSG_DONTWAIT);

	if (bytes_read > 0)
	{
		// Check for buffer overflow protection
		if (read_buffer_.size() + bytes_read > config_.max_read_buffer_size)
		{
			Logger::warn("Read buffer overflow for client {}, closing", client_addr_);
			active_ = false;
			return false;
		}

		read_buffer_.append(buffer, bytes_read);
		last_activity_ = time(nullptr);

		// Update metrics
		auto &metrics = Metrics::getInstance();
		metrics.updateReadBufferSize(read_buffer_.size());

		processRequests();
		return true;
	}
	else if (bytes_read == 0)
	{
		// CRITICAL: Client disconnected - close immediately
		Logger::debug("Client disconnected (EOF): {}", client_addr_);
		active_ = false;
		return false;
	}
	else if (bytes_read == -1)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			// No data available - normal case
			return true;
		}
		else if (errno == ECONNRESET || errno == EPIPE)
		{
			// Connection reset by peer
			Logger::debug("Connection reset by peer: {}", client_addr_);
			active_ = false;
			return false;
		}
		else
		{
			// Other errors
			Logger::error("Read error from {}: {}", client_addr_, strerror(errno));
			active_ = false;
			return false;
		}
	}

	return true;
}

bool MultiplexingServer::ClientConnection::writeAvailable()
{
	try
	{
		std::lock_guard<std::mutex> lock(write_mutex_);

		if (write_buffer_.empty())
		{
			// No data to write, disable write notifications
			disableWriteNotifications();
			return true;
		}

		ssize_t bytes_sent = send(fd_, write_buffer_.data(), write_buffer_.size(), MSG_NOSIGNAL);

		if (bytes_sent > 0)
		{
			write_buffer_.erase(0, bytes_sent);
			last_activity_ = time(nullptr);

			// Update metrics
			auto &metrics = Metrics::getInstance();
			metrics.updateWriteBufferSize(write_buffer_.size());

			// If buffer is now empty, disable write notifications
			if (write_buffer_.empty())
			{
				disableWriteNotifications();
			}
		}
		else if (bytes_sent == -1)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				// Would block, keep write notifications enabled
				return true;
			}
			else
			{
				Logger::error("Write error to {}: {}", client_addr_, strerror(errno));
				active_ = false;
				return false;
			}
		}

		return true;
	}
	catch (const std::exception &e)
	{
		Logger::error("Exception in writeAvailable for {}: {}", client_addr_, e.what());
		active_ = false;
		return false;
	}
}

void MultiplexingServer::ClientConnection::sendResponse(std::string response)
{
	try
	{
		std::lock_guard<std::mutex> lock(write_mutex_);

		bool was_empty = write_buffer_.empty();

		// Use move semantics to avoid copying
		if (write_buffer_.empty())
		{
			write_buffer_ = std::move(response);
		}
		else
		{
			write_buffer_.append(std::move(response));
		}

		Logger::debug("Response queued for sending: {} bytes (total buffer: {})",
					  response.length(), write_buffer_.length());

		// Update metrics
		auto &metrics = Metrics::getInstance();
		metrics.updateWriteBufferSize(write_buffer_.size());

		// If buffer was empty, we need to enable write notifications
		if (was_empty && config_.enable_epollout_optimization)
		{
			enableWriteNotifications();
		}

		// Attempt immediate write
		if (!write_buffer_.empty())
		{
			ssize_t bytes_sent = send(fd_, write_buffer_.data(), write_buffer_.size(),
									  MSG_NOSIGNAL | MSG_DONTWAIT);

			if (bytes_sent > 0)
			{
				Logger::debug("Immediately sent {} bytes", bytes_sent);
				write_buffer_.erase(0, bytes_sent);

				if (write_buffer_.empty() && config_.enable_epollout_optimization)
				{
					disableWriteNotifications();
				}
			}
			else if (bytes_sent == -1)
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK)
				{
					Logger::debug("Send would block, {} bytes remain in buffer", write_buffer_.length());
					// Write notifications will handle the rest
				}
				else
				{
					Logger::error("Send error: {}", strerror(errno));
					active_ = false;
				}
			}
		}
	}
	catch (const std::exception &e)
	{
		Logger::error("Exception in sendResponse for {}: {}", client_addr_, e.what());
		active_ = false;
	}
}

void MultiplexingServer::ClientConnection::enableWriteNotifications()
{
	if (server_)
	{
		server_->enableClientWrite(fd_);
	}
}

void MultiplexingServer::ClientConnection::disableWriteNotifications()
{
	if (server_)
	{
		server_->disableClientWrite(fd_);
	}
}

void MultiplexingServer::ClientConnection::close()
{
	if (fd_ != -1)
	{
		// CRITICAL: Properly shutdown socket before close
		// This completes the TCP handshake and prevents CLOSE_WAIT
		shutdown(fd_, SHUT_RDWR);

		// Track connection duration
		auto connection_duration = time(nullptr) - connection_start_time_;
		auto &metrics = Metrics::getInstance();
		metrics.updateConnectionDuration(static_cast<double>(connection_duration));

		::close(fd_);
		fd_ = -1;
		active_ = false;

		Logger::debug("Connection fully closed: {}", client_addr_);
	}
}

void MultiplexingServer::ClientConnection::processRequests()
{
	size_t pos = 0;
	while (pos < read_buffer_.length())
	{
		// Look for the end of headers
		size_t header_end = read_buffer_.find("\r\n\r\n", pos);
		if (header_end == std::string::npos)
		{
			// Incomplete headers, wait for more data
			break;
		}

		// Parse headers to get Content-Length
		std::string headers_str = read_buffer_.substr(pos, header_end - pos + 4);
		size_t content_length = 0;

		// Extract Content-Length from headers
		size_t cl_pos = headers_str.find("Content-Length:");
		if (cl_pos != std::string::npos)
		{
			size_t cl_end = headers_str.find("\r\n", cl_pos);
			std::string cl_str = headers_str.substr(cl_pos + 15, cl_end - cl_pos - 15);
			// Remove any whitespace
			cl_str.erase(0, cl_str.find_first_not_of(" \t"));
			cl_str.erase(cl_str.find_last_not_of(" \t") + 1);
			try
			{
				content_length = std::stoul(cl_str);
			}
			catch (const std::exception &e)
			{
				Logger::error("Invalid Content-Length: {}", cl_str);
				break;
			}
		}

		// Check if we have the complete body
		size_t body_start = header_end + 4;
		size_t total_request_length = body_start + content_length;

		if (read_buffer_.length() < total_request_length)
		{
			// Incomplete body, wait for more data
			break;
		}

		// Extract complete request
		std::string complete_request = read_buffer_.substr(pos, total_request_length);

		// Use thread pool for request processing
		if (server_ && server_->thread_pool_)
		{
			// Offload to thread pool
			server_->thread_pool_->enqueue([this,
											complete_request = std::move(complete_request),
											client_addr = client_addr_]()
										   {
				try {
					// Parse and handle HTTP request
					std::string method, path, body;
					std::unordered_map<std::string, std::string> headers;
					if (parseHttpRequestOptimized(complete_request, method, path, body, headers)) {
						std::string response_content = handleHttpRequest(method, path, body);
						sendResponse(response_content);
					} else {
						Logger::error("Failed to parse HTTP request from {}", client_addr);
						std::string error_response = createHttpResponse(
							R"({"error": "Invalid HTTP request", "success": false})",
							"application/json", 400);
						sendResponse(error_response);
					}
				} catch (const std::exception& e) {
					Logger::error("Exception in request processing for {}: {}", client_addr, e.what());
					std::string error_response = createHttpResponse(
						R"({"error": "Internal server error", "success": false})",
						"application/json", 500);
					sendResponse(error_response);
				} });
		}
		else
		{
			// Process inline (fallback)
			std::string method, path, body;
			std::unordered_map<std::string, std::string> headers;
			if (parseHttpRequest(complete_request, method, path, body, headers))
			{
				std::string response_content = handleHttpRequest(method, path, body);
				sendResponse(response_content);
			}
			else
			{
				Logger::error("Failed to parse HTTP request from {}", client_addr_);
				std::string error_response = createHttpResponse(
					R"({"error": "Invalid HTTP request", "success": false})",
					"application/json", 400);
				sendResponse(error_response);
			}
		}

		// Move to next request in buffer
		pos = total_request_length;
	}

	// Remove processed requests from buffer
	if (pos > 0)
	{
		read_buffer_.erase(0, pos);
	}

	// Prevent buffer overflow (redundant check for safety)
	if (read_buffer_.length() > config_.max_read_buffer_size)
	{
		Logger::warn("Client buffer too large, clearing. addr: {}", client_addr_);
		read_buffer_.clear();
	}
}

bool MultiplexingServer::ClientConnection::parseHttpRequest(const std::string &data, std::string &method,
															std::string &path, std::string &body,
															std::unordered_map<std::string, std::string> &headers)
{
	return parseHttpRequestOptimized(std::string_view(data.data(), data.length()),
									 method, path, body, headers);
}

bool MultiplexingServer::ClientConnection::parseHttpRequestOptimized(std::string_view data,
																	 std::string &method,
																	 std::string &path,
																	 std::string &body,
																	 std::unordered_map<std::string, std::string> &headers)
{
	// Reset outputs
	method.clear();
	path.clear();
	body.clear();
	headers.clear();

	// Find first line efficiently
	size_t first_line_end = data.find("\r\n");
	if (first_line_end == std::string_view::npos)
	{
		Logger::error("No CRLF found in request");
		return false;
	}

	// Parse request line using string_view for zero-copy
	std::string_view request_line = data.substr(0, first_line_end);

	// Use efficient tokenization
	size_t first_space = request_line.find(' ');
	if (first_space == std::string_view::npos)
	{
		Logger::error("No space in request line: {}", std::string(request_line));
		return false;
	}

	size_t second_space = request_line.find(' ', first_space + 1);
	if (second_space == std::string_view::npos)
	{
		Logger::error("No second space in request line: {}", std::string(request_line));
		return false;
	}

	method = std::string(request_line.substr(0, first_space));
	path = std::string(request_line.substr(first_space + 1, second_space - first_space - 1));

	Logger::debug("Parsed request: {} {}", method, path);

	// Parse headers
	size_t pos = first_line_end + 2;
	size_t headers_end = std::string_view::npos;

	while (pos < data.length())
	{
		size_t line_end = data.find("\r\n", pos);
		if (line_end == std::string_view::npos)
		{
			break;
		}

		// Empty line indicates end of headers
		if (line_end == pos)
		{
			headers_end = line_end + 2;
			break;
		}

		std::string_view header_line = data.substr(pos, line_end - pos);
		size_t colon_pos = header_line.find(':');
		if (colon_pos != std::string_view::npos)
		{
			std::string_view key = header_line.substr(0, colon_pos);
			std::string_view value = header_line.substr(colon_pos + 1);

			// Trim whitespace more efficiently
			while (!key.empty() && (key.front() == ' ' || key.front() == '\t'))
			{
				key.remove_prefix(1);
			}
			while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
			{
				key.remove_suffix(1);
			}

			while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
			{
				value.remove_prefix(1);
			}
			while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
			{
				value.remove_suffix(1);
			}

			headers[std::string(key)] = std::string(value);
		}

		pos = line_end + 2;
	}

	// Extract body if we found the end of headers
	if (headers_end != std::string_view::npos && headers_end < data.length())
	{
		body = std::string(data.substr(headers_end));
		Logger::debug("Body length: {} bytes", body.length());
	}

	return true;
}

std::string MultiplexingServer::ClientConnection::createHttpResponse(const std::string &content,
																	 const std::string &content_type,
																	 int status_code)
{
	std::stringstream response;

	const char *status_text = "OK";
	if (status_code == 400)
		status_text = "Bad Request";
	else if (status_code == 404)
		status_text = "Not Found";
	else if (status_code == 500)
		status_text = "Internal Server Error";

	response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
	response << "Content-Type: " << content_type << "\r\n";
	response << "Content-Length: " << content.length() << "\r\n";

	// CRITICAL FIX: Change from 'close' to 'keep-alive'
	response << "Connection: keep-alive\r\n";
	response << "Keep-Alive: timeout=30, max=1000\r\n";
	response << "Access-Control-Allow-Origin: *\r\n";
	response << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
	response << "Access-Control-Allow-Headers: Content-Type\r\n";
	response << "\r\n";
	response << content;

	std::string response_str = response.str();
	Logger::debug("Created HTTP response: {} {} (total {} bytes)",
				  status_code, status_text, response_str.length());

	return response_str;
}

std::string MultiplexingServer::ClientConnection::handleHttpRequest(const std::string &method,
																	const std::string &path,
																	const std::string &body)
{
	auto &metrics = Metrics::getInstance();

	if (method == "GET")
	{
		if (path == "/health")
		{
			Logger::debug("Health check request from {}", client_addr_);
			std::string json_content = R"({"status": "healthy", "success": true})";
			return createHttpResponse(json_content, "application/json", 200);
		}
		else if (path == "/metrics")
		{
			Logger::debug("Metrics request from {}", client_addr_);
			std::string metrics_content = metrics.getPrometheusMetrics();
			return createHttpResponse(metrics_content, "text/plain", 200);
		}
		else if (path == "/numbers/sum")
		{
			Logger::debug("Total numbers sum request from {}", client_addr_);
			auto total_sum = request_handler_->getTotalNumbersSum();
			std::string json_content = R"({"total_numbers_sum": )" + std::to_string(total_sum) + R"(, "success": true})";
			return createHttpResponse(json_content, "application/json", 200);
		}
		else if (path.find("/numbers/sum/") == 0)
		{
			std::string client_id = path.substr(13);
			Logger::debug("Client numbers sum request for: {} from {}", client_id, client_addr_);
			auto client_sum = request_handler_->getClientNumbersSum(client_id);
			std::string json_content = R"({"client_id": ")" + client_id + R"(", "numbers_sum": )" + std::to_string(client_sum) + R"(, "success": true})";
			return createHttpResponse(json_content, "application/json", 200);
		}
		else if (path == "/numbers/sum-all")
		{
			Logger::debug("All clients numbers sum request from {}", client_addr_);
			auto all_sums = request_handler_->getAllClientSums();

			std::stringstream ss;
			ss << R"({"success": true, "clients": {)";
			bool first = true;
			for (const auto &[client_id, sum] : all_sums)
			{
				if (!first)
					ss << ",";
				ss << R"(")" << client_id << R"(":)" << sum;
				first = false;
			}
			ss << R"(}, "total":)" << request_handler_->getTotalNumbersSum() << "}";
			return createHttpResponse(ss.str(), "application/json", 200);
		}
		else if (path == "/")
		{
			Logger::debug("Root endpoint request from {}", client_addr_);
			std::string json_content = R"({
				"service": "C++ JSON Processing Service",
				"version": "1.0.0",
				"endpoints": {
					"GET /": "API documentation",
					"GET /health": "Service health check",
					"GET /metrics": "Prometheus metrics", 
					"GET /numbers/sum": "Get total sum of all processed numbers",
					"GET /numbers/sum/{client_id}": "Get sum of numbers for specific client",
					"GET /numbers/sum-all": "Get sums for all clients",
					"POST /process": "Process JSON request synchronously",
					"POST /process-async": "Process JSON request asynchronously"
				}
			})";
			return createHttpResponse(json_content, "application/json", 200);
		}
	}
	else if (method == "POST" && path == "/process")
	{
		auto start_time = std::chrono::steady_clock::now();
		metrics.incrementRequests();
		metrics.incrementBytesReceived(body.size());

		if (body.empty())
		{
			Logger::warn("Empty request body from {}", client_addr_);
			metrics.incrementFailedRequests();
			std::string error_json = R"({"error": "Empty request body", "success": false})";
			return createHttpResponse(error_json, "application/json", 400);
		}

		try
		{
			std::string json_response = request_handler_->processRequest(body);
			metrics.incrementSuccessfulRequests();
			metrics.incrementBytesSent(json_response.length());

			auto end_time = std::chrono::steady_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
			double duration_seconds = duration.count() / 1000000.0;
			metrics.updateRequestDuration(duration_seconds);
			metrics.updateRequestDurationHistogram(duration_seconds);

			return createHttpResponse(json_response, "application/json", 200);
		}
		catch (const std::exception &e)
		{
			metrics.incrementFailedRequests();
			Logger::error("Request processing error from {}: {}", client_addr_, e.what());

			auto end_time = std::chrono::steady_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
			double duration_seconds = duration.count() / 1000000.0;
			metrics.updateRequestDuration(duration_seconds);
			metrics.updateRequestDurationHistogram(duration_seconds);

			std::string error_json = R"({"error": "Internal server error", "success": false})";
			return createHttpResponse(error_json, "application/json", 500);
		}
	}

	std::string error_json = R"({"error": "Endpoint not found", "success": false})";
	return createHttpResponse(error_json, "application/json", 404);
}

MultiplexingServer::ThreadPool::ThreadPool(size_t threads)
{
	for (size_t i = 0; i < threads; ++i)
	{
		workers_.emplace_back([this]
							  {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    condition_.wait(lock, [this] {
                        return stop_ || !tasks_.empty();
                    });
                    
                    if (stop_ && tasks_.empty()) return;
                    
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            } });
	}
}

MultiplexingServer::ThreadPool::~ThreadPool()
{
	stop_ = true;
	condition_.notify_all();
	for (std::thread &worker : workers_)
	{
		if (worker.joinable())
		{
			worker.join();
		}
	}
}

// MultiplexingServer implementation
MultiplexingServer::MultiplexingServer(std::string host, int port)
	: host_(std::move(host)), port_(port), server_fd_(-1), epoll_fd_(-1)
{
	global_instance_ = this;
	std::signal(SIGINT, MultiplexingServer::handleSignal);
	std::signal(SIGTERM, MultiplexingServer::handleSignal);
}

MultiplexingServer::~MultiplexingServer()
{
	stop();
	if (global_instance_ == this)
	{
		global_instance_ = nullptr;
	}
}

bool MultiplexingServer::start()
{
	if (isRunning())
	{
		Logger::warn("Server is already running on {}", getAddress());
		return true;
	}

	try
	{
		initializeServer();
		server_thread_ = std::thread(&MultiplexingServer::runServer, this);

		// Wait for server to start
		const auto start_time = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - start_time < std::chrono::milliseconds(2000))
		{
			if (running_)
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		Logger::error("Server failed to start within timeout");
		cleanup();
		return false;
	}
	catch (const std::exception &e)
	{
		Logger::critical("Failed to start server: {}", e.what());
		cleanup();
		return false;
	}
}

bool MultiplexingServer::run()
{
	if (!start())
		return false;

	Logger::info("Multiplexing server running. Press Ctrl+C to stop.");

	while (!shutdown_requested_)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	stop();
	return true;
}

void MultiplexingServer::stop()
{
	if (shutdown_requested_)
		return;

	Logger::info("Initiating multiplexing server shutdown...");
	shutdown_requested_ = true;

	if (server_thread_.joinable())
	{
		server_thread_.join();
	}

	cleanup();
}

bool MultiplexingServer::createSocket()
{
	server_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (server_fd_ < 0)
	{
		Logger::error("Failed to create socket: {}", strerror(errno));
		return false;
	}

	// Set socket options
	int opt = 1;
	if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
	{
		Logger::error("Failed to set SO_REUSEADDR: {}", strerror(errno));
		// Continue anyway
	}

	if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
	{
		Logger::error("Failed to set SO_REUSEPORT: {}", strerror(errno));
		// Continue anyway
	}

	// Bind socket
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port_);

	if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0)
	{
		Logger::error("Failed to bind socket to port {}: {}", port_, strerror(errno));
		close(server_fd_);
		return false;
	}

	// Listen
	if (listen(server_fd_, 1024) < 0)
	{
		Logger::error("Failed to listen on socket: {}", strerror(errno));
		close(server_fd_);
		return false;
	}

	return true;
}

void MultiplexingServer::initializeServer()
{
	Logger::initialize();
	request_handler_ = std::make_unique<RequestHandler>();
	shutdown_requested_ = false;

	// Create connection pool
	connection_pool_ = std::make_unique<ConnectionPool>(config_, this);

	// Create thread pool
	size_t thread_count = config_.thread_pool_size > 0 ? config_.thread_pool_size : std::max(1u, std::thread::hardware_concurrency());
	thread_pool_ = std::make_unique<ThreadPool>(thread_count);
	Logger::info("Created thread pool with {} threads", thread_count);

	if (!createSocket())
	{
		throw std::runtime_error("Failed to create server socket");
	}

	// Create epoll instance
	epoll_fd_ = epoll_create1(0);
	if (epoll_fd_ < 0)
	{
		throw std::runtime_error("Failed to create epoll instance");
	}

	// Add server socket to epoll - use level-triggered for better compatibility
	addToEpoll(server_fd_, EPOLLIN);
}

void MultiplexingServer::runServer()
{
	try
	{
		Logger::info("Multiplexing server thread starting on {}", getAddress());
		running_ = true;
		ready_ = true;

		std::vector<struct epoll_event> events(config_.epoll_max_events);
		time_t last_health_check = time(nullptr);

		while (!shutdown_requested_)
		{
			int num_events = epoll_wait(epoll_fd_, events.data(), config_.epoll_max_events, 1000);

			if (num_events < 0)
			{
				if (errno == EINTR)
					continue; // Interrupted by signal
				Logger::error("epoll_wait error: {}", strerror(errno));
				break;
			}

			for (int i = 0; i < num_events; ++i)
			{
				int fd = events[i].data.fd;
				uint32_t event_flags = events[i].events;

				if (fd == server_fd_)
				{
					handleNewConnection();
				}
				else
				{
					handleClientEvent(fd, event_flags);
				}
				time_t now = time(nullptr);
				if (now - last_health_check >= 5)
				{
					checkConnectionHealth();
					cleanupInactiveClients();
					last_health_check = now;
				}
			}

			// Clean up inactive clients
			cleanupInactiveClients();
		}

		running_ = false;
		ready_ = false;
		Logger::info("Multiplexing server stopped");
	}
	catch (const std::exception &e)
	{
		running_ = false;
		ready_ = false;
		Logger::critical("Server thread exception: {}", e.what());
	}
}

void MultiplexingServer::handleNewConnection()
{
	struct sockaddr_in address;
	socklen_t addrlen = sizeof(address);

	while (true)
	{
		int client_fd = accept4(server_fd_, (struct sockaddr *)&address, &addrlen, SOCK_NONBLOCK);
		if (client_fd < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				break; // No more connections
			}
			Logger::error("Failed to accept connection: {}", strerror(errno));
			continue;
		}

		char client_addr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &address.sin_addr, client_addr, INET_ADDRSTRLEN);
		std::string client_addr_str = std::string(client_addr) + ":" + std::to_string(ntohs(address.sin_port));

		// Use connection pool to get client connection
		auto client = connection_pool_->acquire(client_fd, client_addr_str, request_handler_.get());

		{
			std::lock_guard<std::mutex> lock(clients_mutex_);
			clients_[client_fd] = client;
		}

		// Add to epoll for read events initially
		addToEpoll(client_fd, EPOLLIN | EPOLLRDHUP);

		// Track connection metrics
		auto &metrics = Metrics::getInstance();
		metrics.incrementConnections();

		Logger::debug("New client connected: {}", client_addr_str);
	}
}

void MultiplexingServer::handleClientEvent(int client_fd, uint32_t events)
{
	std::shared_ptr<ClientConnection> client;
	{
		std::lock_guard<std::mutex> lock(clients_mutex_);
		auto it = clients_.find(client_fd);
		if (it == clients_.end())
		{
			// Connection already closed - remove from epoll
			removeFromEpoll(client_fd);
			return;
		}
		client = it->second;
	}

	if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
	{
		Logger::debug("Closing connection due to error/hangup: fd={}, events=0x{:x}", client_fd, events);
		closeClient(client_fd);
		return;
	}

	// Handle read events
	if (events & EPOLLIN)
	{
		if (!client->readAvailable())
		{
			closeClient(client_fd);
			return;
		}
	}

	// Handle write events
	if (events & EPOLLOUT)
	{
		if (!client->writeAvailable())
		{
			closeClient(client_fd);
			return;
		}
	}
}

void MultiplexingServer::enableClientWrite(int client_fd)
{
	modifyEpoll(client_fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP);
}

void MultiplexingServer::disableClientWrite(int client_fd)
{
	modifyEpoll(client_fd, EPOLLIN | EPOLLRDHUP);
}

void MultiplexingServer::closeClient(int client_fd)
{
	std::shared_ptr<ClientConnection> client;
	{
		std::lock_guard<std::mutex> lock(clients_mutex_);
		auto it = clients_.find(client_fd);
		if (it == clients_.end())
		{
			// Already closed
			return;
		}
		client = it->second;
		clients_.erase(it);
	}

	// Remove from epoll first
	removeFromEpoll(client_fd);

	// CRITICAL: Properly close the connection
	if (client)
	{
		client->close(); // Ensure ClientConnection::close() is called
	}

	// Return connection to pool for reuse
	connection_pool_->release(client);

	// Track connection metrics
	auto &metrics = Metrics::getInstance();
	metrics.decrementConnections();

	Logger::debug("Client fully closed: {}", client->getClientAddress());
}

void MultiplexingServer::cleanupInactiveClients()
{
	const time_t TIMEOUT = config_.connection_timeout;
	time_t now = time(nullptr);

	std::lock_guard<std::mutex> lock(clients_mutex_);
	for (auto it = clients_.begin(); it != clients_.end();)
	{
		if (now - it->second->getLastActivity() > TIMEOUT)
		{
			Logger::info("Closing inactive client: {}", it->second->getClientAddress());
			removeFromEpoll(it->first);

			// Return to pool
			connection_pool_->release(it->second);
			it = clients_.erase(it);

			auto &metrics = Metrics::getInstance();
			metrics.decrementConnections();
		}
		else
		{
			++it;
		}
	}
}

void MultiplexingServer::addToEpoll(int fd, uint32_t events)
{
	struct epoll_event event;
	event.events = events;
	event.data.fd = fd;

	if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) < 0)
	{
		throw std::runtime_error("Failed to add fd to epoll");
	}
}

void MultiplexingServer::modifyEpoll(int fd, uint32_t events)
{
	struct epoll_event event;
	event.events = events;
	event.data.fd = fd;

	if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) < 0)
	{
		// It's possible the fd was already closed, log and continue
		Logger::debug("Failed to modify fd {} in epoll: {}", fd, strerror(errno));
	}
}

void MultiplexingServer::removeFromEpoll(int fd)
{
	// Don't throw if epoll_ctl fails - fd might already be closed
	epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

void MultiplexingServer::cleanup()
{
	Logger::info("Cleaning up multiplexing server resources...");

	// Close all client connections
	{
		std::lock_guard<std::mutex> lock(clients_mutex_);
		for (auto &[client_fd, client] : clients_)
		{
			removeFromEpoll(client_fd);
		}
		clients_.clear();
	}

	// Clean up connection pool
	connection_pool_.reset();

	// Close server socket
	if (server_fd_ >= 0)
	{
		close(server_fd_);
		server_fd_ = -1;
	}

	// Close epoll
	if (epoll_fd_ >= 0)
	{
		close(epoll_fd_);
		epoll_fd_ = -1;
	}

	// Clean up thread pool
	thread_pool_.reset();

	// Clean up request handler
	if (request_handler_)
	{
		request_handler_.reset();
	}

	// Reset state
	running_ = false;
	ready_ = false;

	Logger::info("Multiplexing server shutdown complete");
	Logger::shutdown();
}

void MultiplexingServer::checkConnectionHealth()
{
	std::vector<int> dead_connections;
	time_t now = time(nullptr);

	{
		std::lock_guard<std::mutex> lock(clients_mutex_);
		for (const auto &[fd, client] : clients_)
		{
			// More aggressive detection of CLOSE_WAIT state
			char buffer[1];
			ssize_t result = recv(fd, buffer, 1, MSG_PEEK | MSG_DONTWAIT);

			if (result == 0)
			{
				// Connection in CLOSE_WAIT state (peer sent FIN)
				dead_connections.push_back(fd);
				Logger::debug("Health check: fd={} in CLOSE_WAIT - closing", fd);
			}
			else if (result < 0)
			{
				if (errno == ECONNRESET || errno == EPIPE || errno == EBADF)
				{
					// Connection error states
					dead_connections.push_back(fd);
					Logger::debug("Health check: fd={} error: {}", fd, strerror(errno));
				}
				// Don't close on EAGAIN/EWOULDBLOCK - these are normal
			}

			// Check for inactive connections (reduce timeout)
			if (now - client->getLastActivity() > 30)
			{ // 30 second timeout instead of 60
				dead_connections.push_back(fd);
				Logger::debug("Health check: fd={} inactive ({}s)",
							  fd, now - client->getLastActivity());
			}
		}
	}

	// Close dead connections
	for (int fd : dead_connections)
	{
		Logger::info("Health check closing connection: fd={}", fd);
		closeClient(fd);
	}

	if (!dead_connections.empty())
	{
		Logger::info("Health check removed {} connections", dead_connections.size());
	}
}