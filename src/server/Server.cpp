#include <iostream>
#include <csignal>
#include <chrono>
#include <utility>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef HAS_SCTP
#include <netinet/sctp.h>
#endif

#include <logging/Logger.h>
#include <server/Metrics.h>
#include <server/Server.h>

// Initialize static member
Server *Server::global_instance_ = nullptr;

// Static member function for signal handling
void Server::handleSignal(int signal)
{
	Logger::info("Received signal: {}", signal);
	if (global_instance_)
	{
		global_instance_->stop();
	}
}

Server::Server(std::string host, int port, Protocol protocol)
	: host_(std::move(host)), port_(port), protocol_(protocol)
{
	// Register for signal handling
	global_instance_ = this;

	// Setup signal handling using static member function
	std::signal(SIGINT, Server::handleSignal);
	std::signal(SIGTERM, Server::handleSignal);
}

Server::~Server()
{
	stop();
	if (global_instance_ == this)
	{
		global_instance_ = nullptr;
	}
}

bool Server::createSocket()
{
	if (protocol_ == Protocol::HTTP)
	{
		// HTTP uses httplib, no raw socket needed
		return true;
	}

	server_fd_ = socket(AF_INET,
						ProtocolFactory::getSocketType(protocol_),
						ProtocolFactory::getSocketProtocol(protocol_));

	if (server_fd_ < 0)
	{
		Logger::error("Failed to create {} socket: {}",
					  ProtocolFactory::protocolToString(protocol_),
					  strerror(errno));
		return false;
	}

	// Set socket options
	int opt = 1;
	if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
	{
		Logger::error("Failed to set SO_REUSEADDR: {}", strerror(errno));
		// Continue anyway
	}

	// Bind socket
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port_);

	if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0)
	{
		Logger::error("Failed to bind {} socket to port {}: {}",
					  ProtocolFactory::protocolToString(protocol_),
					  port_, strerror(errno));
		close(server_fd_);
		server_fd_ = -1;
		return false;
	}

	// Listen for stream protocols
	if (ProtocolFactory::isStreamProtocol(protocol_) && protocol_ != Protocol::HTTP)
	{
		if (listen(server_fd_, 1024) < 0)
		{
			Logger::error("Failed to listen on {} socket: {}",
						  ProtocolFactory::protocolToString(protocol_),
						  strerror(errno));
			close(server_fd_);
			server_fd_ = -1;
			return false;
		}
	}

	return true;
}

bool Server::start()
{
	if (isRunning())
	{
		Logger::warn("Server is already running on {}", getAddress());
		return true;
	}

	try
	{
		initializeServer();

		// Start server thread
		server_thread_ = std::thread(&Server::runServer, this);

		// Wait for server to start
		if (!waitForThreadStart())
		{
			Logger::error("Server failed to start within timeout");
			cleanup();
			return false;
		}

		Logger::info("Server started successfully on {}", getAddress());
		return true;
	}
	catch (const std::exception &e)
	{
		Logger::critical("Failed to start server: {}", e.what());
		cleanup();
		return false;
	}
}

bool Server::run()
{
	if (!start())
	{
		return false;
	}

	Logger::info("Server running. Press Ctrl+C to stop.");

	// Main loop - wait for shutdown signal
	while (!shutdown_requested_)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	stop();
	return true;
}

void Server::stop()
{
	if (shutdown_requested_)
	{
		return; // Already shutting down
	}

	Logger::info("Initiating server shutdown...");
	shutdown_requested_ = true;

	// Stop the HTTP server first
	if (server_)
	{
		server_->stop();
	}

	// Close raw socket if used
	if (server_fd_ >= 0)
	{
		close(server_fd_);
		server_fd_ = -1;
	}

	// Wait for server thread to finish
	if (server_thread_.joinable())
	{
		server_thread_.join();
	}

	cleanup();
}

void Server::runServer()
{
	try
	{
		Logger::info("Server thread starting on {}", getAddress());
		running_ = true;
		ready_ = true;

		if (protocol_ == Protocol::HTTP)
		{
			// Start listening - this blocks until server stops
			const bool listen_success = server_->listen(host_.c_str(), port_);

			// Server stopped listening
			running_ = false;
			ready_ = false;

			if (listen_success)
			{
				Logger::info("Server stopped listening (shutdown requested)");
			}
			else
			{
				Logger::error("Server failed to start listening on {}", getAddress());
			}
		}
		else
		{
			// Handle raw protocols
			Logger::info("Raw {} server running", ProtocolFactory::protocolToString(protocol_));

			while (!shutdown_requested_)
			{
				if (protocol_ == Protocol::UDP)
				{
					handleUDPMessage();
				}
				else if (protocol_ == Protocol::SCTP)
				{
					handleSCTPMessage();
				}
				else if (protocol_ == Protocol::TCP)
				{
					handleRawTCPMessage();
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}

			running_ = false;
			ready_ = false;
		}
	}
	catch (const std::exception &e)
	{
		running_ = false;
		ready_ = false;
		Logger::critical("Server thread exception: {}", e.what());
	}
}

void Server::handleUDPMessage()
{
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	char buffer[4096];

	ssize_t bytes_received = recvfrom(server_fd_, buffer, sizeof(buffer) - 1, 0,
									  (struct sockaddr *)&client_addr, &client_len);

	if (bytes_received > 0)
	{
		buffer[bytes_received] = '\0';
		std::string message(buffer, bytes_received);

		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

		Logger::debug("UDP message from {}:{} - {} bytes",
					  client_ip, ntohs(client_addr.sin_port), bytes_received);

		// Process the message (simple echo for demonstration)
		std::string response = "Echo: " + message;
		sendto(server_fd_, response.c_str(), response.length(), 0,
			   (struct sockaddr *)&client_addr, client_len);
	}
}

void Server::handleSCTPMessage()
{
#ifdef HAS_SCTP
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	char buffer[4096];

	ssize_t bytes_received = sctp_recvmsg(server_fd_, buffer, sizeof(buffer) - 1,
										  (struct sockaddr *)&client_addr, &client_len,
										  NULL, 0, NULL, NULL);

	if (bytes_received > 0)
	{
		buffer[bytes_received] = '\0';
		std::string message(buffer, bytes_received);

		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

		Logger::debug("SCTP message from {}:{} - {} bytes",
					  client_ip, ntohs(client_addr.sin_port), bytes_received);

		// Process the message (simple echo for demonstration)
		std::string response = "Echo: " + message;
		sctp_sendmsg(server_fd_, response.c_str(), response.length(),
					 (struct sockaddr *)&client_addr, client_len,
					 0, 0, 0, 0, 0);
	}
#else
	Logger::warn("SCTP support not compiled in");
	std::this_thread::sleep_for(std::chrono::seconds(1));
#endif
}

void Server::handleRawTCPMessage()
{
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	int client_fd = accept(server_fd_, (struct sockaddr *)&client_addr, &client_len);

	if (client_fd >= 0)
	{
		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
		Logger::debug("TCP connection from {}:{}", client_ip, ntohs(client_addr.sin_port));

		char buffer[4096];
		ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

		if (bytes_received > 0)
		{
			buffer[bytes_received] = '\0';
			std::string message(buffer, bytes_received);
			Logger::debug("TCP message: {} bytes", bytes_received);

			// Process the message (simple echo for demonstration)
			std::string response = "Echo: " + message;
			send(client_fd, response.c_str(), response.length(), 0);
		}

		close(client_fd);
	}
}

void Server::initializeServer()
{
	Logger::initialize();

	if (protocol_ == Protocol::HTTP)
	{
		// Initialize HTTP server components
		request_handler_ = std::make_unique<RequestHandler>();
		server_ = std::make_unique<httplib::Server>();

		// Configure server
		server_->new_task_queue = []
		{
			return new httplib::ThreadPool(std::thread::hardware_concurrency());
		};

		// Set timeouts
		server_->set_read_timeout(30, 0);  // 30 seconds
		server_->set_write_timeout(30, 0); // 30 seconds

		setupRoutes();
	}
	else
	{
		// Initialize raw protocol server
		if (!createSocket())
		{
			throw std::runtime_error("Failed to create " +
									 ProtocolFactory::protocolToString(protocol_) + " socket");
		}

		request_handler_ = std::make_unique<RequestHandler>();
	}

	shutdown_requested_ = false;
}

void Server::cleanup()
{
	Logger::info("Cleaning up server resources...");

	// Clean up request handler first (it might use logger)
	if (request_handler_)
	{
		request_handler_.reset();
	}

	// Clean up server
	if (server_)
	{
		server_.reset();
	}

	// Close socket if used
	if (server_fd_ >= 0)
	{
		close(server_fd_);
		server_fd_ = -1;
	}

	// Reset state flags
	running_ = false;
	ready_ = false;

	Logger::info("Server shutdown complete");

	// Shutdown logger LAST - ensure no other threads are using it
	Logger::shutdown();
}

bool Server::waitForThreadStart(int timeout_ms)
{
	const auto start_time = std::chrono::steady_clock::now();
	const auto timeout = std::chrono::milliseconds(timeout_ms);

	while (std::chrono::steady_clock::now() - start_time < timeout)
	{
		if (running_)
		{
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return false;
}

void Server::setupRoutes()
{
	if (!server_ || !request_handler_)
	{
		return;
	}

	// Health check endpoint
	server_->Get("/health", [](const httplib::Request &, httplib::Response &res)
				 {
        Logger::debug("Health check request");
        res.set_content(R"({"status": "healthy", "success": true})", "application/json"); });

	// Metrics endpoint
	server_->Get("/metrics", [](const httplib::Request &, httplib::Response &res)
				 {
        Logger::debug("Metrics request");
        auto& metrics = Metrics::getInstance();
        res.set_content(metrics.getPrometheusMetrics(), "text/plain"); });

	server_->Get("/numbers/sum", [this](const httplib::Request &, httplib::Response &res)
				 {
    Logger::debug("Total numbers sum request");
    auto total_sum = request_handler_->getTotalNumbersSum();
    res.set_content(R"({"total_numbers_sum": )" + std::to_string(total_sum) + R"(, "success": true})", "application/json"); });

	// Client numbers tracking endpoint
	server_->Get("/numbers/sum/(.*)", [this](const httplib::Request &req, httplib::Response &res)
				 {
    std::string client_id = req.matches[1];
    Logger::debug("Client numbers sum request for: {}", client_id);
    auto client_sum = request_handler_->getClientNumbersSum(client_id);
    res.set_content(R"({"client_id": ")" + client_id + R"(", "numbers_sum": )" + std::to_string(client_sum) + R"(, "success": true})", "application/json"); });

	// All clients numbers endpoint
	server_->Get("/numbers/sum-all", [this](const httplib::Request &, httplib::Response &res)
				 {
    Logger::debug("All clients numbers sum request");
    auto all_sums = request_handler_->getAllClientSums();
    
    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType &allocator = doc.GetAllocator();
    
    doc.AddMember("success", true, allocator);
    
    rapidjson::Value clients(rapidjson::kObjectType);
    for (const auto& [client_id, sum] : all_sums) {
        clients.AddMember(
            rapidjson::Value(client_id.c_str(), allocator).Move(),
            rapidjson::Value(static_cast<int64_t>(sum)), // Fix: cast to int64_t
            allocator
        );
    }
    doc.AddMember("clients", clients, allocator);
    doc.AddMember("total", rapidjson::Value(static_cast<int64_t>(request_handler_->getTotalNumbersSum())), allocator); // Fix: cast to int64_t
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    res.set_content(buffer.GetString(), "application/json"); });

	// Root endpoint - API documentation
	server_->Get("/", [](const httplib::Request &, httplib::Response &res)
				 {
        Logger::debug("Root endpoint request");
        res.set_content(R"({
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
        })", "application/json"); });

	// Synchronous processing endpoint - UPDATED with metrics
	server_->Post("/process", [this](const httplib::Request &req, httplib::Response &res)
				  {    
        auto& metrics = Metrics::getInstance();
        metrics.incrementRequests();
        metrics.incrementBytesReceived(req.body.size());
        
        auto start_time = std::chrono::steady_clock::now();
        
        if (req.body.empty()) {
            Logger::warn("Empty request body");
            metrics.incrementFailedRequests();
            res.status = 400;
            std::string error_response = R"({"error": "Empty request body", "success": false})";
            metrics.incrementBytesSent(error_response.size());
            res.set_content(error_response, "application/json");
            return;
        }
        
        try {
            auto response = request_handler_->processRequest(req.body);
            metrics.incrementSuccessfulRequests();
            metrics.incrementBytesSent(response.size());
            res.set_content(std::move(response), "application/json");
        } 
        catch (const std::exception& e) {
            metrics.incrementFailedRequests();
            Logger::error("Request processing error: {}", e.what());
            res.status = 500;
            std::string error_response = R"({"error": "Internal server error", "success": false})";
            metrics.incrementBytesSent(error_response.size());
            res.set_content(error_response, "application/json");
        }
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        double duration_seconds = duration.count() / 1000000.0;
        metrics.updateRequestDuration(duration_seconds);
        metrics.updateRequestDurationHistogram(duration_seconds); });

	// Asynchronous processing endpoint - UPDATED with metrics
	server_->Post("/process-async", [this](const httplib::Request &req, httplib::Response &res)
				  {
        auto& metrics = Metrics::getInstance();
        metrics.incrementRequests();
        metrics.incrementBytesReceived(req.body.size());
        
        auto start_time = std::chrono::steady_clock::now();
        
        if (req.body.empty()) {
            Logger::warn("Empty request body");
            metrics.incrementFailedRequests();
            res.status = 400;
            std::string error_response = R"({"error": "Empty request body", "success": false})";
            metrics.incrementBytesSent(error_response.size());
            res.set_content(error_response, "application/json");
            return;
        }
        
        try {
            auto future_response = request_handler_->processRequestAsync(req.body);
            auto response = future_response.get();
            metrics.incrementSuccessfulRequests();
            metrics.incrementBytesSent(response.size());
            res.set_content(std::move(response), "application/json");
            Logger::info("Async request processed successfully");
        } 
        catch (const std::exception& e) {
            metrics.incrementFailedRequests();
            Logger::error("Async request processing error: {}", e.what());
            res.status = 500;
            std::string error_response = R"({"error": "Internal server error", "success": false})";
            metrics.incrementBytesSent(error_response.size());
            res.set_content(error_response, "application/json");
        }
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        double duration_seconds = duration.count() / 1000000.0;
        metrics.updateRequestDuration(duration_seconds);
        metrics.updateRequestDurationHistogram(duration_seconds); });

	// Connection tracking - Add connection callbacks
	server_->set_pre_routing_handler([&](const httplib::Request & /*req*/, httplib::Response & /*res*/)
									 {
        auto& metrics = Metrics::getInstance();
        metrics.incrementConnections();
        return httplib::Server::HandlerResponse::Unhandled; });

	server_->set_post_routing_handler([&](const httplib::Request & /*req*/, httplib::Response & /*res*/)
									  {
        auto& metrics = Metrics::getInstance();
        metrics.decrementConnections();
        return httplib::Server::HandlerResponse::Unhandled; });

	// 404 handler
	server_->set_error_handler([](const httplib::Request &, httplib::Response &res)
							   {
        if (res.status == 404) {
            Logger::warn("404 - Endpoint not found");
            auto& metrics = Metrics::getInstance();
            metrics.incrementBytesSent(res.body.size());
            res.set_content(R"({"error": "Endpoint not found", "success": false})", "application/json");
        } });
}