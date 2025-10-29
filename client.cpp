#include <iostream>
#include <string>
#include <client/Client.h>

int main(int argc, char *argv[])
{
	std::string host = "localhost";
	int port = 8080;

	if (argc >= 2)
		host = argv[1];
	if (argc >= 3)
		port = std::stoi(argv[2]);

	std::cout << "Connecting to " << host << ":" << port << std::endl;

	try
	{
		Client client(host, port);

		// Simple health check
		auto response = client.sendRequest("/health", "GET");
		std::cout << "Server response: " << response << std::endl;

		std::string sync_data = R"({"id": 123, "name": "Test User", "phone": "+1234567890", "number": 42})";
		auto sync_response = client.sendRequest("/process", "POST", sync_data);
        std::cout << "Sync: " << sync_response << std::endl;
        std::cout << std::endl;

		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
}