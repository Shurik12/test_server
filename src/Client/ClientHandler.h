#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/SocketStream.h>

#include <iostream>
#include <string>

class ClientHandler
{

public:
	ClientHandler(std::string h, int port);

	bool connected();
	bool sendMessage();
	bool sendMessageTestLoad(std::string message);
	bool recieveMessage();

private:
	std::string host;
	int port;

	// IP endpoint/socket address (consists of host addr and port #)
	Poco::Net::SocketAddress socketAddr;

	// Interface to a TCP stream socket
	Poco::Net::StreamSocket socket;

	// Stream for reading from / writing to a socket (accepts a socket)
	Poco::Net::SocketStream stream;
};

