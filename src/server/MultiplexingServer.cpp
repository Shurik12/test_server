#include <iostream>
#include <csignal>
#include <chrono>
#include <sstream>
#include <cstring>
#include <algorithm>

#include <logging/Logger.h>
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

	// Check if we have a complete HTTP request
	size_t header_end = client.buffer.find("\r\n\r\n");
	if (header_end != std::string::npos)
	{
		std::string method, path, body;
		if (parseHttpRequest(client.buffer, method, path, body))
		{
			processRequest(client_fd, client.buffer);
			client.buffer.clear();
		}
	}
}

void MultiplexingServer::closeClient(int client_fd)
{
	FD_CLR(client_fd, &master_fds_);
	close(client_fd);
	clients_.erase(client_fd);
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
	if (method == "GET")
	{
		if (path == "/health")
		{
			return R"({"status": "healthy", "success": true})";
		}
		else if (path == "/metrics")
		{
			return R"({"active_connections": 0, "requests_processed": 0, "success": true})";
		}
		else if (path == "/")
		{
			return R"({
                "service": "C++ JSON Processing Service",
                "version": "1.0.0",
                "endpoints": {
                    "GET /": "API documentation",
                    "GET /health": "Service health check",
                    "GET /metrics": "Service metrics", 
                    "POST /process": "Process JSON request"
                }
            })";
		}
	}
	else if (method == "POST" && path == "/process")
	{
		if (body.empty())
		{
			return R"({"error": "Empty request body", "success": false})";
		}
		return request_handler_->processRequest(body);
	}

	return R"({"error": "Endpoint not found", "success": false})";
}

std::string MultiplexingServer::createHttpResponse(const std::string &content)
{
	std::stringstream response;
	response << "HTTP/1.1 200 OK\r\n";
	response << "Content-Type: application/json\r\n";
	response << "Content-Length: " << content.length() << "\r\n";
	response << "Connection: keep-alive\r\n";
	response << "\r\n";
	response << content;

	return response.str();
}

void MultiplexingServer::processRequest(int client_fd, const std::string &request)
{
	std::string method, path, body;
	if (parseHttpRequest(request, method, path, body))
	{
		std::string response_content = handleHttpRequest(method, path, body);
		std::string http_response = createHttpResponse(response_content);

		send(client_fd, http_response.c_str(), http_response.length(), 0);
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