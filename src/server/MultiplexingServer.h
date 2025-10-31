#pragma once

#include <server/IServer.h>
#include <server/RequestHandler.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>

class MultiplexingServer : public IServer
{
public:
	MultiplexingServer(std::string host = "0.0.0.0", int port = 8080);
	~MultiplexingServer();

	// IServer implementation
	bool start() override;
	void stop() override;
	bool run() override;

	bool isRunning() const noexcept override { return running_; }
	bool isReady() const noexcept override { return running_ && ready_; }

	const std::string &getHost() const noexcept override { return host_; }
	int getPort() const noexcept override { return port_; }
	std::string getAddress() const override { return host_ + ":" + std::to_string(port_); }

	std::string getType() const override { return "multiplexing"; }

private:
	struct Client
	{
		int fd;
		std::string buffer;
		time_t last_activity;
	};

	void runServer();
	void initializeServer();
	void cleanup();
	bool createSocket();
	void handleNewConnection();
	void handleClientData(int client_fd);
	void closeClient(int client_fd);
	void processRequest(int client_fd, const std::string &request);
	std::string getSystemInfo();
	std::string createHttpResponse(const std::string &content,
								   const std::string &content_type = "application/json");
	// HTTP request parsing
	bool parseHttpRequest(const std::string &data, std::string &method,
						  std::string &path, std::string &body);
	std::string handleHttpRequest(const std::string &method,
								  const std::string &path, const std::string &body);

	// Server configuration
	const std::string host_;
	const int port_;

	// Server state
	std::atomic<bool> running_{false};
	std::atomic<bool> ready_{false};
	std::atomic<bool> shutdown_requested_{false};

	// Server components
	int server_fd_;
	std::unique_ptr<RequestHandler> request_handler_;
	std::thread server_thread_;

	// Client management
	std::unordered_map<int, Client> clients_;
	fd_set master_fds_;
	int max_fd_;

	static MultiplexingServer *global_instance_;
	static void handleSignal(int signal);
};