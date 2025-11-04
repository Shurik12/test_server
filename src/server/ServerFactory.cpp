#include <server/ServerFactory.h>
#include <config/Config.h>
#include <vector>
#include <memory>

class MultiProtocolServer : public IServer
{
public:
	MultiProtocolServer(const std::string &host, int port)
		: host_(host), port_(port) {}

	bool start() override
	{
		auto enabledProtocols = Config::getEnabledProtocols();

		for (auto protocol : enabledProtocols)
		{
			auto server = createServerForProtocol(protocol);
			if (server)
			{
				servers_.push_back(std::move(server));
			}
		}

		if (servers_.empty())
		{
			return false;
		}

		// Start all servers
		for (auto &server : servers_)
		{
			if (!server->start())
			{
				return false;
			}
		}

		running_ = true;
		return true;
	}

	void stop() override
	{
		for (auto &server : servers_)
		{
			server->stop();
		}
		servers_.clear();
		running_ = false;
	}

	bool run() override
	{
		if (!start())
		{
			return false;
		}

		// Wait for shutdown signal
		while (running_)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}

		return true;
	}

	bool isRunning() const noexcept override { return running_; }
	bool isReady() const noexcept override { return running_; }

	const std::string &getHost() const noexcept override { return host_; }
	int getPort() const noexcept override { return port_; }
	std::string getAddress() const override { return host_ + ":" + std::to_string(port_); }
	std::string getType() const override { return "multiprotocol"; }
	Protocol getProtocol() const override { return Protocol::HTTP; } // Main protocol
	void setProtocol(Protocol /*protocol*/) override { /* Not used for multi-protocol */ }

private:
	std::unique_ptr<IServer> createServerForProtocol(Protocol protocol)
	{
		std::string type = Config::getString("server.type", "blocking");

		if (type == "multiplexing")
		{
			return std::make_unique<MultiplexingServer>(host_, port_, protocol);
		}
		else
		{
			return std::make_unique<Server>(host_, port_, protocol);
		}
	}

	std::string host_;
	int port_;
	std::vector<std::unique_ptr<IServer>> servers_;
	std::atomic<bool> running_{false};
};

std::unique_ptr<IServer> ServerFactory::createServer(const std::string &type,
													 const std::string &host, int port,
													 Protocol protocol)
{
	if (type == "multiplexing")
	{
		return std::make_unique<MultiplexingServer>(host, port, protocol);
	}
	else
	{
		return std::make_unique<Server>(host, port, protocol);
	}
}

std::unique_ptr<IServer> ServerFactory::createFromConfig()
{
	std::string host = Config::getString("server.host", "0.0.0.0");
	int port = Config::getInt("server.port", 8080);
	std::string type = Config::getString("server.type", "blocking");
	std::string protocol_str = Config::getString("server.protocol", "http"); // Default to HTTP
	Protocol protocol = ProtocolFactory::stringToProtocol(protocol_str);

	return createServer(type, host, port, protocol);
}