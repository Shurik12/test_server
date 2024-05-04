#include <iostream>
#include "ClientHandler.h"

int main(int argc, char** argv) 
{
	int port = 1234;
	std::string hostname = "127.0.0.1";

	// Handle the server-client connection and send some json
	ClientHandler handler(hostname, port);
	if(handler.connected())
	{
		// while(true)
		// {
		// 	if (!handler.sendMessage())
		// 		break;
		// 	handler.recieveMessage();
		// }

		for (int i = 0; i <= 1000000; ++i)
		{
			if (!handler.sendMessageTestLoad(std::to_string(i)))
				break;
			handler.recieveMessage();
		}
		handler.sendMessageTestLoad("quit");
	}

	return 0;
}
