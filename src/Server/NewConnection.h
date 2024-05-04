#include <Poco/Net/Socket.h>
#include <Poco/Net/TCPServer.h>
#include <Poco/Net/TCPServerConnection.h>
#include <Poco/Net/TCPServerConnectionFactory.h>
#include <Poco/Net/TCPServerParams.h>

class NewConnection : public Poco::Net::TCPServerConnection
{
public:
		NewConnection(const Poco::Net::StreamSocket & s)
  		    : Poco::Net::TCPServerConnection(s)
  		{}

  		void run() override;
  		int sum(const std::vector<int> & v);
  		void executeOnWorkers(const std::vector<int> & v1, const std::vector<int> & v2);
};
