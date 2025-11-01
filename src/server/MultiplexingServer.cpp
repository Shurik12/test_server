#include <iostream>
#include <csignal>
#include <chrono>
#include <sstream>
#include <cstring>
#include <algorithm>

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

MultiplexingServer::MultiplexingServer(std::string host, int port)
	: host_(std::move(host)), port_(port), server_fd_(-1), max_fd_(0)
{

	global_instance_ = this;
	std::signal(SIGINT, MultiplexingServer::handleSignal);
	std::signal(SIGTERM, MultiplexingServer::handleSignal);

	FD_ZERO(&master_fds_);
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
	server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd_ < 0)
	{
		Logger::error("Failed to create socket");
		return false;
	}

	// Set socket options
	int opt = 1;
	if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
	{
		Logger::error("Failed to set socket options");
		close(server_fd_);
		return false;
	}

	// Make socket non-blocking
	int flags = fcntl(server_fd_, F_GETFL, 0);
	fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

	// Bind socket
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port_);

	if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0)
	{
		Logger::error("Failed to bind socket to port {}", port_);
		close(server_fd_);
		return false;
	}

	// Listen
	if (listen(server_fd_, 10) < 0)
	{
		Logger::error("Failed to listen on socket");
		close(server_fd_);
		return false;
	}

	// Add server socket to master set
	FD_SET(server_fd_, &master_fds_);
	max_fd_ = server_fd_;

	return true;
}

void MultiplexingServer::initializeServer()
{
	Logger::initialize();
	request_handler_ = std::make_unique<RequestHandler>();
	shutdown_requested_ = false;

	if (!createSocket())
	{
		throw std::runtime_error("Failed to create server socket");
	}
}

void MultiplexingServer::runServer()
{
	try
	{
		Logger::info("Multiplexing server thread starting on {}", getAddress());
		running_ = true;
		ready_ = true;

		while (!shutdown_requested_)
		{
			fd_set read_fds = master_fds_;
			struct timeval timeout = {1, 0}; // 1 second timeout

			int activity = select(max_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);

			if (activity < 0 && errno != EINTR)
			{
				Logger::error("Select error: {}", strerror(errno));
				break;
			}

			if (activity == 0)
			{
				// Timeout occurred, check for shutdown
				continue;
			}

			// Check server socket for new connections
			if (FD_ISSET(server_fd_, &read_fds))
			{
				handleNewConnection();
			}

			// Check client sockets for data
			for (auto it = clients_.begin(); it != clients_.end();)
			{
				int client_fd = it->first;

				if (FD_ISSET(client_fd, &read_fds))
				{
					handleClientData(client_fd);

					// If client was closed during handling, move to next
					if (clients_.find(client_fd) == it)
					{
						++it;
					}
					else
					{
						it = clients_.begin(); // Restart iteration if map was modified
					}
				}
				else
				{
					++it;
				}
			}
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

	int client_fd = accept(server_fd_, (struct sockaddr *)&address, &addrlen);

	if (client_fd < 0)
	{
		Logger::error("Failed to accept connection");
		return;
	}

	// Make client socket non-blocking
	int flags = fcntl(client_fd, F_GETFL, 0);
	fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

	// Add to master set and client map
	FD_SET(client_fd, &master_fds_);
	if (client_fd > max_fd_)
	{
		max_fd_ = client_fd;
	}

	clients_[client_fd] = {client_fd, "", time(nullptr)};

	// Track connection metrics
	auto &metrics = Metrics::getInstance();
	metrics.incrementConnections();

	Logger::debug("New client connected: fd {}", client_fd);
}

void MultiplexingServer::handleClientData(int client_fd)
{
	char buffer[4096];
	ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

	if (bytes_read <= 0)
	{
		closeClient(client_fd);
		return;
	}

	buffer[bytes_read] = '\0';
	auto &client = clients_[client_fd];
	client.buffer.append(buffer);
	client.last_activity = time(nullptr);

	// Process all complete requests in the buffer
	size_t request_start = 0;
	while (request_start < client.buffer.length())
	{
		// Look for the end of headers
		size_t header_end = client.buffer.find("\r\n\r\n", request_start);
		if (header_end == std::string::npos)
		{
			// Incomplete headers, wait for more data
			break;
		}

		// Parse headers to get Content-Length
		std::string headers = client.buffer.substr(request_start, header_end - request_start + 4);
		size_t content_length = 0;

		// Extract Content-Length from headers
		size_t cl_pos = headers.find("Content-Length:");
		if (cl_pos != std::string::npos)
		{
			size_t cl_end = headers.find("\r\n", cl_pos);
			std::string cl_str = headers.substr(cl_pos + 15, cl_end - cl_pos - 15);
			content_length = std::stoul(cl_str);
		}

		// Check if we have the complete body
		size_t body_start = header_end + 4;
		size_t total_request_length = body_start + content_length;

		if (client.buffer.length() < total_request_length)
		{
			// Incomplete body, wait for more data
			break;
		}

		// Extract complete request
		std::string complete_request = client.buffer.substr(request_start, total_request_length);

		// Process this complete request
		std::string method, path, body;
		if (parseHttpRequest(complete_request, method, path, body))
		{
			processRequest(client_fd, complete_request);
		}
		else
		{
			Logger::error("Failed to parse complete HTTP request");
			// Send error response
			std::string error_response = createHttpResponse(
				R"({"error": "Invalid HTTP request", "success": false})",
				"application/json");
			send(client_fd, error_response.c_str(), error_response.length(), 0);
		}

		// Move to next request in buffer
		request_start = total_request_length;
	}

	// Remove processed requests from buffer
	if (request_start > 0)
	{
		client.buffer.erase(0, request_start);
	}

	// If buffer is getting too large, clear it to prevent memory issues
	if (client.buffer.length() > 16384)
	{ // 16KB limit
		Logger::warn("Client buffer too large, clearing. fd: {}", client_fd);
		client.buffer.clear();
	}
}

void MultiplexingServer::closeClient(int client_fd)
{
	FD_CLR(client_fd, &master_fds_);
	close(client_fd);
	clients_.erase(client_fd);

	// Track connection metrics
	auto &metrics = Metrics::getInstance();
	metrics.decrementConnections();

	Logger::debug("Client disconnected: fd {}", client_fd);
}

bool MultiplexingServer::parseHttpRequest(const std::string &data, std::string &method,
										  std::string &path, std::string &body)
{
	size_t first_line_end = data.find("\r\n");
	if (first_line_end == std::string::npos)
		return false;

	// Parse request line
	std::string request_line = data.substr(0, first_line_end);
	std::istringstream iss(request_line);
	iss >> method >> path;

	// Find body
	size_t header_end = data.find("\r\n\r\n");
	if (header_end != std::string::npos)
	{
		body = data.substr(header_end + 4);
	}

	return true;
}

std::string MultiplexingServer::handleHttpRequest(const std::string &method,
												  const std::string &path, const std::string &body)
{
	auto &metrics = Metrics::getInstance();

	if (method == "GET")
	{
		if (path == "/health")
		{
			Logger::debug("Health check request");
			return R"({"status": "healthy", "success": true})";
		}
		else if (path == "/metrics")
		{
			Logger::debug("Metrics request");
			return metrics.getPrometheusMetrics();
		}
		else if (method == "GET" && path == "/numbers/sum")
		{
			Logger::debug("Total numbers sum request");
			auto total_sum = request_handler_->getTotalNumbersSum();
			return R"({"total_numbers_sum": )" + std::to_string(total_sum) + R"(, "success": true})";
		}
		else if (method == "GET" && path.find("/numbers/sum/") == 0)
		{
			std::string client_id = path.substr(13); // Remove "/numbers/sum/"
			Logger::debug("Client numbers sum request for: {}", client_id);
			auto client_sum = request_handler_->getClientNumbersSum(client_id);
			return R"({"client_id": ")" + client_id + R"(", "numbers_sum": )" + std::to_string(client_sum) + R"(, "success": true})";
		}
		else if (method == "GET" && path == "/numbers/sum-all")
		{
			Logger::debug("All clients numbers sum request");
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
			Logger::debug("Root endpoint request");
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
			Logger::warn("Empty request body");
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
			Logger::error("Request processing error: {}", e.what());

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

std::string MultiplexingServer::createHttpResponse(const std::string &content, const std::string &content_type)
{
	std::stringstream response;
	response << "HTTP/1.1 200 OK\r\n";
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

void MultiplexingServer::processRequest(int client_fd, const std::string &request)
{
	std::string method, path, body;
	if (parseHttpRequest(request, method, path, body))
	{
		Logger::debug("Processing {} {}", method, path);

		std::string response_content = handleHttpRequest(method, path, body);

		// Set proper content type - FIXED
		std::string content_type = "application/json";
		if (method == "GET" && path == "/metrics")
		{
			content_type = "text/plain; version=0.0.4; charset=utf-8";
			Logger::debug("Setting metrics content type: {}", content_type);
		}

		std::string http_response = createHttpResponse(response_content, content_type);
		ssize_t bytes_sent = send(client_fd, http_response.c_str(), http_response.length(), 0);

		if (bytes_sent < 0)
		{
			Logger::error("Failed to send response to client {}", client_fd);
		}
	}
	else
	{
		Logger::error("Failed to parse HTTP request");
		std::string error_response = createHttpResponse(
			R"({"error": "Invalid HTTP request", "success": false})",
			"application/json");
		send(client_fd, error_response.c_str(), error_response.length(), 0);
	}
}

void MultiplexingServer::cleanup()
{
	Logger::info("Cleaning up multiplexing server resources...");

	// Close all client connections
	for (auto &[client_fd, client] : clients_)
	{
		close(client_fd);
	}
	clients_.clear();

	// Close server socket
	if (server_fd_ >= 0)
	{
		close(server_fd_);
		server_fd_ = -1;
	}

	// Clean up request handler
	if (request_handler_)
	{
		request_handler_.reset();
	}

	// Reset state
	running_ = false;
	ready_ = false;
	FD_ZERO(&master_fds_);
	max_fd_ = 0;

	Logger::info("Multiplexing server shutdown complete");
	Logger::shutdown();
}