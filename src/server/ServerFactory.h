#pragma once

#include <memory>
#include <string>

#include <server/IServer.h>
#include <server/Server.h>
#include <server/MultiplexingServer.h>
#include <server/Protocol.h>

class ServerFactory
{
public:
	static std::unique_ptr<IServer> createServer(const std::string &type = "multiplexing",
												 const std::string &host = "0.0.0.0",
												 int port = 8080,
												 Protocol protocol = Protocol::HTTP);

	static std::unique_ptr<IServer> createFromConfig();
};