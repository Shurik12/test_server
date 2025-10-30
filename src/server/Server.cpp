#include <iostream>
#include <csignal>
#include <chrono>
#include <utility>

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

Server::Server(std::string host, int port)
	: host_(std::move(host)), port_(port)
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
	catch (const std::exception &e)
	{
		running_ = false;
		ready_ = false;
		Logger::critical("Server thread exception: {}", e.what());
	}
}

void Server::initializeServer()
{
	Logger::initialize();

	// Initialize components
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
	server_->set_pre_routing_handler([&](const httplib::Request &/*req*/, httplib::Response &/*res*/)
									 {
        auto& metrics = Metrics::getInstance();
        metrics.incrementConnections();
        return httplib::Server::HandlerResponse::Unhandled; });

	server_->set_post_routing_handler([&](const httplib::Request &/*req*/, httplib::Response &/*res*/)
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