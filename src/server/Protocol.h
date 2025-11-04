#pragma once

#include <string>
#include <netinet/in.h>
#include <sys/socket.h>

enum class Protocol
{
	TCP,
	UDP,
	SCTP,
	HTTP
};

class ProtocolFactory
{
public:
	static int getSocketType(Protocol protocol);
	static int getSocketProtocol(Protocol protocol);
	static std::string protocolToString(Protocol protocol);
	static Protocol stringToProtocol(const std::string &protocolStr);

	static bool isStreamProtocol(Protocol protocol);
	static bool isDatagramProtocol(Protocol protocol);
};