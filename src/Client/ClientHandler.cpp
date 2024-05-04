#include "ClientHandler.h"

ClientHandler::ClientHandler(std::string h, int p) 
	: host(h)
	, port(p)
	, socketAddr(h, p)
	, socket()
	, stream(socket) 
{
	std::cout << "Host: " << host << "\tPort: " << port << "\n";	
}

// Connect to the initialized socket address' hostname and port
bool ClientHandler::connected() 
{
	std::cout << "Creating a connection with [" 
		  << socketAddr.host().toString() 
		  << "] through port [" 
		  << socketAddr.port() 
		  << "] ..."; 
	try 
	{ 
		socket.connect(socketAddr); 
		std::cout << "Success!" << "\n";
	} 
	catch (Poco::Exception err) 
	{
		std::cout << "\n";
		std::cout << "Socket connection error: [" 
			  << err.displayText() << "]" 
			  << "\n";
		return false;
	}
	return true;
}

// Send a message to the connected server
bool ClientHandler::sendMessage() 
{
	std::cout << "\n";
	
	try 
	{ 
		std::string message;

		std::cout << "Enter a message to send to the server: ";
		std::cin >> message;

		if(message.compare("exit") != 0 
				&& message.compare("quit") != 0 
				&& message.compare("q") != 0) 
		{ 
			std::cout << "Sending the message \"" 
				  << message << "\" to the server!" 
				  << std::endl;
			socket.sendBytes(message.data(), message.size()); 
			return true;
		}
		else return false;
	}
	catch (Poco::Exception err) 
	{
		std::cout << "Data send error: [" 
			  << err.displayText() << "]" 
			  << "\n"; 
		return false;
	}
}


// Send a message to the connected server
bool ClientHandler::sendMessageTestLoad(std::string message)
{
	try 
	{
		if(message.compare("exit") != 0 
				&& message.compare("quit") != 0 
				&& message.compare("q") != 0) 
		{ 
			std::cout << "Sending the message \"" 
				  << message << "\" to the server!" 
				  << std::endl;
			socket.sendBytes(message.data(), message.size());
			return true;
		}
		else return false;
	}
	catch (Poco::Exception err) 
	{
		std::cout << "Data send error: [" 
			  << err.displayText() << "]" 
			  << "\n"; 
		return false;
	}
}


bool ClientHandler::recieveMessage()
{
	try
	{
		char buffer[256];
                int n = socket.receiveBytes(buffer, sizeof(buffer));
                std::cout << "Received: " << std::string(buffer, n) << "\n";
		return true;
	}
	catch (Poco::Exception err) 
	{
		std::cout << "Data recieve error: [" 
			  << err.displayText() << "]" 
			  << "\n"; 
		return false;
	}
}
