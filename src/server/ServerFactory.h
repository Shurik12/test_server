#pragma once

#include <memory>
#include <string>

#include <server/IServer.h>
#include <server/Server.h> // Blocking server
#include <server/MultiplexingServer.h>

class ServerFactory
{
public:
	static std::unique_ptr<IServer> createServer(const std::string &type = "blocking",
												 const std::string &host = "0.0.0.0",
												 int port = 8080);

	static std::unique_ptr<IServer> createFromConfig();
};