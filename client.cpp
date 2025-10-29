#include <iostream>
#include <string>

#include <client/Client.h>
#include <config/Config.h>

int main(int argc, char *argv[])
{
	// Load configuration
	Config::loadFromFile("config.yaml");
	Config::loadFromArgs(argc, argv);

	// Get client configuration with defaults
	std::string host = Config::getString("client.host", Config::getString("server.host", "localhost"));
	int port = Config::getInt("client.port", Config::getInt("server.port", 8080));

	std::cout << "Connecting to " << host << ":" << port << std::endl;

	std::string request_data;
	std::string response;
	try
	{
		Client client(host, port);

		// Simple health check
		auto response = client.sendRequest("/health", "GET");
		std::cout << "Server response: " << response << std::endl;

		std::string sync_data = R"({"id": 123, "name": "Test User", "phone": "+1234567890", "number": 42})";
		auto sync_response = client.sendRequest("/process", "POST", sync_data);
		std::cout << "Sync: " << sync_response << "\n\n";

		while (true)
		{
			std::cout << "\nInput json (or 'q' to quit): ";
			std::getline(std::cin, request_data);

			request_data.erase(0, request_data.find_first_not_of(" \t\n\r\f\v"));
			request_data.erase(request_data.find_last_not_of(" \t\n\r\f\v") + 1);

			if (request_data == "q" || request_data == "Q" || request_data.empty())
				break;

			try
			{
				auto response = client.sendRequest("/process", "POST", request_data);
				std::cout << "Response: " << response << "\n";
			}
			catch (const std::exception &e)
			{
				std::cerr << "Request failed: " << e.what() << std::endl;
			}
		}

		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
}