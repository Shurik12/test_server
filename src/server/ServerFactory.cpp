#include <server/ServerFactory.h>
#include <config/Config.h>
#include <logging/Logger.h>

std::unique_ptr<IServer> ServerFactory::createServer(const std::string &type,
													 const std::string &host, int port,
													 Protocol protocol)
{
	if (type == "multiplexing")
	{
		Logger::info("Creating MultiplexingServer: {} on {}:{}",
					 ProtocolFactory::protocolToString(protocol), host, port);
		return std::make_unique<MultiplexingServer>(host, port, protocol);
	}
	else
	{
		Logger::info("Creating Blocking Server: {} on {}:{}",
					 ProtocolFactory::protocolToString(protocol), host, port);
		return std::make_unique<Server>(host, port, protocol);
	}
}

std::unique_ptr<IServer> ServerFactory::createFromConfig()
{
	std::string host = Config::getString("host", "0.0.0.0");
	std::string type = Config::getString("type", "multiplexing");
	Protocol protocol = Config::getProtocol();
	int port = Config::getPort();

	return createServer(type, host, port, protocol);
}