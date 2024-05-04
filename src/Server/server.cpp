#include "NewConnection.h"
#include <iostream>
#include <vector>

using namespace std;

int main()
{
    // Create the server socket to listen
  	Poco::Net::ServerSocket socket(1234);

    // Configure some server parameters
  	Poco::Net::TCPServerParams *pParams = new Poco::Net::TCPServerParams();
  	pParams->setMaxThreads(4);
  	pParams->setMaxQueued(4);
  	pParams->setThreadIdleTime(100);

    // Create the server
  	Poco::Net::TCPServer myserver(
  	    new Poco::Net::TCPServerConnectionFactoryImpl<NewConnection>(), socket,
  	    pParams);
  	myserver.start();
	std::cout << "Server is started!" << "\n";

  	while (true)
		;
    return 0;
}
