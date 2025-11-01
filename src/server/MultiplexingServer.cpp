#include <iostream>
#include <csignal>
#include <chrono>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <system_error>

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
MultiplexingServer::ClientConnection::ClientConnection(int fd, const std::string &client_addr, RequestHandler *request_handler)
	: fd_(fd), client_addr_(client_addr), last_activity_(time(nullptr)), request_handler_(request_handler)
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

bool MultiplexingServer::ClientConnection::readAvailable()
{
	char buffer[4096];
	ssize_t bytes_read;

	while ((bytes_read = recv(fd_, buffer, sizeof(buffer) - 1, 0)) > 0)
	{
		buffer[bytes_read] = '\0';
		read_buffer_.append(buffer, bytes_read);
		last_activity_ = time(nullptr);

		// Process complete requests
		processRequests();
	}

	if (bytes_read == 0 || (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK))
	{
		active_ = false;
		return false; // Connection closed or error
	}

	return true;
}

bool MultiplexingServer::ClientConnection::writeAvailable()
{
	std::lock_guard<std::mutex> lock(write_mutex_);

	if (write_buffer_.empty())
	{
		return true; // Nothing to write
	}

	ssize_t bytes_sent = send(fd_, write_buffer_.data(), write_buffer_.size(), MSG_NOSIGNAL);

	if (bytes_sent > 0)
	{
		write_buffer_.erase(0, bytes_sent);
		last_activity_ = time(nullptr);
	}
	else if (bytes_sent == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
	{
		active_ = false;
		return false; // Error
	}

	return true;
}

void MultiplexingServer::ClientConnection::sendResponse(const std::string &response)
{
	std::lock_guard<std::mutex> lock(write_mutex_);
	write_buffer_ += response;
}

void MultiplexingServer::ClientConnection::close()
{
	if (fd_ != -1)
	{
		::close(fd_);
		fd_ = -1;
		active_ = false;
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

		// Parse and handle HTTP request
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

		// Move to next request in buffer
		pos = total_request_length;
	}

	// Remove processed requests from buffer
	if (pos > 0)
	{
		read_buffer_.erase(0, pos);
	}

	// Prevent buffer overflow
	if (read_buffer_.length() > 65536)
	{ // 64KB limit
		Logger::warn("Client buffer too large, clearing. addr: {}", client_addr_);
		read_buffer_.clear();
	}
}

bool MultiplexingServer::ClientConnection::parseHttpRequest(const std::string &data, std::string &method,
															std::string &path, std::string &body,
															std::unordered_map<std::string, std::string> &headers)
{
	size_t first_line_end = data.find("\r\n");
	if (first_line_end == std::string::npos)
		return false;

	// Parse request line
	std::string request_line = data.substr(0, first_line_end);
	std::istringstream iss(request_line);
	iss >> method >> path;

	// Parse headers
	size_t pos = first_line_end + 2;
	while (pos < data.length())
	{
		size_t line_end = data.find("\r\n", pos);
		if (line_end == std::string::npos)
			break;

		if (line_end == pos)
		{ // Empty line - end of headers
			pos = line_end + 2;
			break;
		}

		std::string header_line = data.substr(pos, line_end - pos);
		size_t colon_pos = header_line.find(':');
		if (colon_pos != std::string::npos)
		{
			std::string key = header_line.substr(0, colon_pos);
			std::string value = header_line.substr(colon_pos + 1);
			// Trim whitespace
			key.erase(0, key.find_first_not_of(" \t"));
			key.erase(key.find_last_not_of(" \t") + 1);
			value.erase(0, value.find_first_not_of(" \t"));
			value.erase(value.find_last_not_of(" \t") + 1);
			headers[key] = value;
		}

		pos = line_end + 2;
	}

	// Extract body
	if (pos < data.length())
	{
		body = data.substr(pos);
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
	response << "Connection: keep-alive\r\n";
	response << "Access-Control-Allow-Origin: *\r\n";
	response << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
	response << "Access-Control-Allow-Headers: Content-Type\r\n";
	response << "\r\n";
	response << content;

	return response.str();
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
			return R"({"status": "healthy", "success": true})";
		}
		else if (path == "/metrics")
		{
			Logger::debug("Metrics request from {}", client_addr_);
			return metrics.getPrometheusMetrics();
		}
		else if (path == "/numbers/sum")
		{
			Logger::debug("Total numbers sum request from {}", client_addr_);
			auto total_sum = request_handler_->getTotalNumbersSum();
			return R"({"total_numbers_sum": )" + std::to_string(total_sum) + R"(, "success": true})";
		}
		else if (path.find("/numbers/sum/") == 0)
		{
			std::string client_id = path.substr(13);
			Logger::debug("Client numbers sum request for: {} from {}", client_id, client_addr_);
			auto client_sum = request_handler_->getClientNumbersSum(client_id);
			return R"({"client_id": ")" + client_id + R"(", "numbers_sum": )" + std::to_string(client_sum) + R"(, "success": true})";
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
			return ss.str();
		}
		else if (path == "/")
		{
			Logger::debug("Root endpoint request from {}", client_addr_);
			return R"({
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
			return R"({"error": "Empty request body", "success": false})";
		}

		try
		{
			std::string response = request_handler_->processRequest(body);
			metrics.incrementSuccessfulRequests();
			metrics.incrementBytesSent(response.size());

			auto end_time = std::chrono::steady_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
			double duration_seconds = duration.count() / 1000000.0;
			metrics.updateRequestDuration(duration_seconds);
			metrics.updateRequestDurationHistogram(duration_seconds);

			return response;
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

			return R"({"error": "Internal server error", "success": false})";
		}
	}

	// 404 - Not Found
	return R"({"error": "Endpoint not found", "success": false})";
}

// ThreadPool implementation
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

template <class F>
void MultiplexingServer::ThreadPool::enqueue(F &&task)
{
	{
		std::lock_guard<std::mutex> lock(queue_mutex_);
		tasks_.emplace(std::forward<F>(task));
	}
	condition_.notify_one();
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
		Logger::error("Failed to set socket options: {}", strerror(errno));
		close(server_fd_);
		return false;
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
	{ // Increased backlog
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

	// Create thread pool
	size_t thread_count = std::max(1u, std::thread::hardware_concurrency());
	thread_pool_ = std::make_unique<ThreadPool>(thread_count);

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

	// Add server socket to epoll
	addToEpoll(server_fd_, EPOLLIN | EPOLLET); // Edge-triggered
}

void MultiplexingServer::runServer()
{
	try
	{
		Logger::info("Multiplexing server thread starting on {}", getAddress());
		running_ = true;
		ready_ = true;

		const int MAX_EVENTS = 64;
		struct epoll_event events[MAX_EVENTS];

		while (!shutdown_requested_)
		{
			int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1000); // 1s timeout

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
	{ // Accept all pending connections (edge-triggered)
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

		auto client = std::make_shared<ClientConnection>(client_fd, client_addr_str, request_handler_.get());

		{
			std::lock_guard<std::mutex> lock(clients_mutex_);
			clients_[client_fd] = client;
		}

		// Add to epoll for read events
		addToEpoll(client_fd, EPOLLIN | EPOLLET | EPOLLRDHUP);

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
			return;
		client = it->second;
	}

	if (events & EPOLLRDHUP || events & EPOLLHUP || events & EPOLLERR)
	{
		closeClient(client_fd);
		return;
	}

	if (events & EPOLLIN)
	{
		if (!client->readAvailable())
		{
			closeClient(client_fd);
			return;
		}
	}

	if (events & EPOLLOUT)
	{
		if (!client->writeAvailable())
		{
			closeClient(client_fd);
			return;
		}
	}
}

void MultiplexingServer::closeClient(int client_fd)
{
	std::shared_ptr<ClientConnection> client;
	{
		std::lock_guard<std::mutex> lock(clients_mutex_);
		auto it = clients_.find(client_fd);
		if (it == clients_.end())
			return;
		client = it->second;
		clients_.erase(it);
	}

	removeFromEpoll(client_fd);
	client->close();

	// Track connection metrics
	auto &metrics = Metrics::getInstance();
	metrics.decrementConnections();

	Logger::debug("Client disconnected: {}", client->getClientAddress());
}

void MultiplexingServer::cleanupInactiveClients()
{
	const time_t TIMEOUT = 300; // 5 minutes
	time_t now = time(nullptr);

	std::lock_guard<std::mutex> lock(clients_mutex_);
	for (auto it = clients_.begin(); it != clients_.end();)
	{
		if (now - it->second->getLastActivity() > TIMEOUT)
		{
			Logger::info("Closing inactive client: {}", it->second->getClientAddress());
			removeFromEpoll(it->first);
			it->second->close();
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
		throw std::runtime_error("Failed to modify fd in epoll");
	}
}

void MultiplexingServer::removeFromEpoll(int fd)
{
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
			client->close();
		}
		clients_.clear();
	}

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