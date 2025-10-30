#include <server/ServerFactory.h>
#include <config/Config.h>

std::unique_ptr<IServer> ServerFactory::createServer(const std::string &type,
													 const std::string &host, int port)
{
	if (type == "multiplexing")
	{
		return std::unique_ptr<IServer>(new MultiplexingServer(host, port));
	}
	else
	{
		return std::unique_ptr<IServer>(new Server(host, port));
	}
}

std::unique_ptr<IServer> ServerFactory::createFromConfig()
{
	std::string host = Config::getString("server.host", "0.0.0.0");
	int port = Config::getInt("server.port", 8080);
	std::string type = Config::getString("server.type", "blocking");

	return createServer(type, host, port);
}