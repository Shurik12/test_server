#pragma once

#include <server/IServer.h>
#include <server/RequestHandler.h>
#include <server/Metrics.h>
#include <server/Protocol.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
#include <string_view>

#ifdef HAS_SCTP
#include <netinet/sctp.h>
#endif

class MultiplexingServer : public IServer
{
public:
	MultiplexingServer(std::string host = "0.0.0.0", int port = 8080, Protocol protocol = Protocol::TCP);
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
	Protocol getProtocol() const override { return protocol_; }
	void setProtocol(Protocol protocol) override { protocol_ = protocol; }

	void checkConnectionHealth();

private:
	struct ServerConfig
	{
		size_t max_read_buffer_size = 65536;
		size_t max_write_buffer_size = 65536;
		int connection_timeout = 60;
		int epoll_max_events = 512;
		int epoll_timeout_ms = 50;
		size_t thread_pool_size = std::max(8u, std::thread::hardware_concurrency() * 4);
		bool enable_epollout_optimization = true;
		size_t max_concurrent_connections = 10000;
		int request_timeout = 10;
		int udp_max_datagram_size = 65507;
	};

	class ClientConnection
	{
	public:
		ClientConnection(int fd, const std::string &client_addr, RequestHandler *request_handler,
						 const ServerConfig &config, MultiplexingServer *server, Protocol protocol);
		~ClientConnection();

		int getFd() const { return fd_; }
		const std::string &getClientAddress() const { return client_addr_; }
		Protocol getProtocol() const { return protocol_; }

		bool readAvailable();
		bool writeAvailable();
		void sendResponse(std::string response);
		void sendUdpResponse(const std::string &response, const sockaddr_in &client_addr);
		void close();
		bool isActive() const { return active_; }
		time_t getLastActivity() const { return last_activity_; }
		bool hasDataToSend() const
		{
			std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(write_mutex_));
			return !write_buffer_.empty();
		}
		void processDatagram(const std::string &data, const sockaddr_in *client_addr = nullptr);
		void reset(int fd, const std::string &client_addr, RequestHandler *request_handler, Protocol protocol);

	private:
		void processRequests();
		void processCompleteHttpRequest(std::string complete_request);
		std::string handleHttpRequest(const std::string &method,
									  const std::string &path,
									  const std::string &body);
		std::string handleRawRequest(const std::string &data);
		bool parseHttpRequest(const std::string &data, std::string &method,
							  std::string &path, std::string &body,
							  std::unordered_map<std::string, std::string> &headers);
		bool parseHttpRequestOptimized(std::string_view data, std::string &method,
									   std::string &path, std::string &body,
									   std::unordered_map<std::string, std::string> &headers);
		std::string createHttpResponse(const std::string &content,
									   const std::string &content_type = "application/json",
									   int status_code = 200);
		void enableWriteNotifications();
		void disableWriteNotifications();

		int fd_;
		std::string client_addr_;
		std::string read_buffer_;
		mutable std::mutex write_mutex_;
		std::string write_buffer_;
		std::atomic<bool> active_{true};
		time_t last_activity_;
		time_t connection_start_time_;
		RequestHandler *request_handler_;
		const ServerConfig &config_;
		MultiplexingServer *server_;
		Protocol protocol_;
		sockaddr_in udp_client_addr_; // For UDP responses
		bool has_udp_client_{false};
	};

	// Connection pool for reusing ClientConnection objects
	class ConnectionPool
	{
	private:
		std::vector<std::shared_ptr<ClientConnection>> pool_;
		std::mutex pool_mutex_;
		const ServerConfig &config_;
		MultiplexingServer *server_;

	public:
		ConnectionPool(const ServerConfig &config, MultiplexingServer *server)
			: config_(config), server_(server) {}

		std::shared_ptr<ClientConnection> acquire(int fd, const std::string &addr,
												  RequestHandler *handler, Protocol protocol)
		{
			std::lock_guard<std::mutex> lock(pool_mutex_);
			if (!pool_.empty())
			{
				auto conn = pool_.back();
				pool_.pop_back();
				conn->reset(fd, addr, handler, protocol);
				return conn;
			}
			return std::make_shared<ClientConnection>(fd, addr, handler, config_, server_, protocol);
		}

		void release(std::shared_ptr<ClientConnection> conn)
		{
			std::lock_guard<std::mutex> lock(pool_mutex_);
			if (pool_.size() < 100)
			{
				pool_.push_back(std::move(conn));
			}
		}
	};

	void runServer();
	void initializeServer();
	void cleanup();
	bool createSocket();
	void handleNewConnection();
	void handleClientEvent(int client_fd, uint32_t events);
	void handleUdpDatagram();
	void closeClient(int client_fd);
	void addToEpoll(int fd, uint32_t events);
	void modifyEpoll(int fd, uint32_t events);
	void removeFromEpoll(int fd);
	void cleanupInactiveClients();
	void enableClientWrite(int client_fd);
	void disableClientWrite(int client_fd);

	// Server configuration
	const std::string host_;
	const int port_;
	Protocol protocol_;
	ServerConfig config_;

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
	std::unique_ptr<ConnectionPool> connection_pool_;

	// UDP-specific
	std::unordered_map<std::string, std::shared_ptr<ClientConnection>> udp_clients_;
	std::mutex udp_clients_mutex_;

	class ThreadPool
	{
	private:
		std::vector<std::thread> workers_;
		std::queue<std::function<void()>> tasks_;
		mutable std::mutex queue_mutex_;
		std::condition_variable condition_;
		std::atomic<bool> stop_{false};

	public:
		explicit ThreadPool(size_t threads);
		~ThreadPool();

		template <class F>
		void enqueue(F &&task);

		size_t getQueueSize() const
		{
			std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(queue_mutex_));
			return tasks_.size();
		}
	};

	std::unique_ptr<ThreadPool> thread_pool_;

	static MultiplexingServer *global_instance_;
	static void handleSignal(int signal);
};

// ThreadPool template implementation must be in header
template <class F>
void MultiplexingServer::ThreadPool::enqueue(F &&task)
{
	{
		std::lock_guard<std::mutex> lock(queue_mutex_);
		tasks_.emplace(std::forward<F>(task));
	}
	condition_.notify_one();
}