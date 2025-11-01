#pragma once

#include <server/IServer.h>
#include <server/RequestHandler.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // Added for inet_ntop
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <queue>
#include <functional>
#include <condition_variable>

class MultiplexingServer : public IServer
{
public:
	MultiplexingServer(std::string host = "0.0.0.0", int port = 8080);
	~MultiplexingServer();

	// Delete copy operations
	MultiplexingServer(const MultiplexingServer &) = delete;
	MultiplexingServer &operator=(const MultiplexingServer &) = delete;

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
	class ClientConnection
	{
	public:
		ClientConnection(int fd, const std::string &client_addr, RequestHandler *request_handler);
		~ClientConnection();

		int getFd() const { return fd_; }
		const std::string &getClientAddress() const { return client_addr_; }

		bool readAvailable();
		bool writeAvailable();
		void sendResponse(const std::string &response);
		void close();
		bool isActive() const { return active_; }
		time_t getLastActivity() const { return last_activity_; }

	private:
		void processRequests();
		std::string handleHttpRequest(const std::string &method,
									  const std::string &path,
									  const std::string &body);
		bool parseHttpRequest(const std::string &data, std::string &method,
							  std::string &path, std::string &body,
							  std::unordered_map<std::string, std::string> &headers);
		std::string createHttpResponse(const std::string &content,
									   const std::string &content_type = "application/json",
									   int status_code = 200);

		int fd_;
		std::string client_addr_;
		std::string read_buffer_;
		std::string write_buffer_;
		std::mutex write_mutex_;
		std::atomic<bool> active_{true};
		time_t last_activity_;
		RequestHandler *request_handler_; // Reference to parent's request handler
	};

	void runServer();
	void initializeServer();
	void cleanup();
	bool createSocket();
	void handleNewConnection();
	void handleClientEvent(int client_fd, uint32_t events);
	void closeClient(int client_fd);
	void addToEpoll(int fd, uint32_t events);
	void modifyEpoll(int fd, uint32_t events);
	void removeFromEpoll(int fd);
	void cleanupInactiveClients(); // Added declaration

	// Server configuration
	const std::string host_;
	const int port_;

	// Server state
	std::atomic<bool> running_{false};
	std::atomic<bool> ready_{false};
	std::atomic<bool> shutdown_requested_{false};

	// Server components
	int server_fd_;
	int epoll_fd_;
	std::unique_ptr<RequestHandler> request_handler_;
	std::thread server_thread_;

	// Client management
	std::unordered_map<int, std::shared_ptr<ClientConnection>> clients_;
	std::mutex clients_mutex_;

	// Thread pool for request processing
	class ThreadPool
	{
	private:
		std::vector<std::thread> workers_;
		std::queue<std::function<void()>> tasks_;
		std::mutex queue_mutex_;
		std::condition_variable condition_;
		std::atomic<bool> stop_{false};

	public:
		explicit ThreadPool(size_t threads);
		~ThreadPool();

		template <class F>
		void enqueue(F &&task);
	};

	std::unique_ptr<ThreadPool> thread_pool_;

	static MultiplexingServer *global_instance_;
	static void handleSignal(int signal);
};