#include <server/Protocol.h>
#include <netinet/sctp.h>

int ProtocolFactory::getSocketType(Protocol protocol)
{
	switch (protocol)
	{
	case Protocol::TCP:
	case Protocol::SCTP:
	case Protocol::HTTP:
		return SOCK_STREAM;
	case Protocol::UDP:
		return SOCK_DGRAM;
	default:
		return SOCK_STREAM;
	}
}

int ProtocolFactory::getSocketProtocol(Protocol protocol)
{
	switch (protocol)
	{
	case Protocol::TCP:
		return IPPROTO_TCP;
	case Protocol::UDP:
		return IPPROTO_UDP;
	case Protocol::SCTP:
		return IPPROTO_SCTP;
	case Protocol::HTTP:
		return IPPROTO_TCP; // HTTP runs over TCP
	default:
		return 0;
	}
}

std::string ProtocolFactory::protocolToString(Protocol protocol)
{
	switch (protocol)
	{
	case Protocol::TCP:
		return "tcp";
	case Protocol::UDP:
		return "udp";
	case Protocol::SCTP:
		return "sctp";
	case Protocol::HTTP:
		return "http";
	default:
		return "tcp";
	}
}

Protocol ProtocolFactory::stringToProtocol(const std::string &protocolStr)
{
	if (protocolStr == "tcp")
		return Protocol::TCP;
	if (protocolStr == "udp")
		return Protocol::UDP;
	if (protocolStr == "sctp")
		return Protocol::SCTP;
	if (protocolStr == "http")
		return Protocol::HTTP;
	return Protocol::TCP; // default
}

bool ProtocolFactory::isStreamProtocol(Protocol protocol)
{
	return protocol == Protocol::TCP || protocol == Protocol::SCTP || protocol == Protocol::HTTP;
}

bool ProtocolFactory::isDatagramProtocol(Protocol protocol)
{
	return protocol == Protocol::UDP;
}