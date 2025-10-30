#pragma once

#include <atomic>
#include <thread>
#include <memory>
#include <string>

#include <common/httplib.h>
#include <server/RequestHandler.h>
#include <server/IServer.h>

class Server : public IServer
{
public:
	Server(std::string host = "0.0.0.0", int port = 8080);
	~Server();

	// Delete copy operations
	Server(const Server &) = delete;
	Server &operator=(const Server &) = delete;

	// Move operations
	Server(Server &&) = default;
	Server &operator=(Server &&) = default;

	// IServer implementation
	bool start() override;
	bool run() override;
	void stop() override;

	// Status queries
	bool isRunning() const noexcept override { return running_; }
	bool isReady() const noexcept override { return running_ && ready_; }

	// Server information
	const std::string &getHost() const noexcept override { return host_; }
	int getPort() const noexcept override { return port_; }
	std::string getAddress() const override { return host_ + ":" + std::to_string(port_); }
	std::string getType() const override { return "blocking"; }

private:
	void setupRoutes();
	void runServer();
	void initializeServer();
	void cleanup();
	bool waitForThreadStart(int timeout_ms = 2000);

	// Signal handling
	static void handleSignal(int signal);

	// Server configuration
	const std::string host_;
	const int port_;

	// Server state
	std::atomic<bool> running_{false};
	std::atomic<bool> ready_{false};
	std::atomic<bool> shutdown_requested_{false};

	// Server components
	std::unique_ptr<httplib::Server> server_;
	std::unique_ptr<RequestHandler> request_handler_;
	std::thread server_thread_;

	// Signal handling - make it non-static instance pointer
	static Server *global_instance_;
};