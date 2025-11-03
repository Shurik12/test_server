#include <client/Client.h>
#include <spdlog/spdlog.h>

Client::Client(const std::string &host, int port)
	: host(host), port(port), client(host, port)
{
	client.set_connection_timeout(10);
	client.set_read_timeout(30);
	client.set_write_timeout(10);
	client.set_keep_alive(true);
	client.set_follow_location(true);
}

std::string Client::sendRequest(const std::string &endpoint, const std::string &method,
								const std::string &body, const std::string &content_type)
{
	try
	{
		if (method == "GET")
		{
			auto response = client.Get(endpoint);
			if (response && response->status == 200)
			{
				return response->body;
			}
			else
			{
				throw std::runtime_error("GET request failed: " +
										 (response ? std::to_string(response->status) : "no response"));
			}
		}
		else if (method == "POST")
		{
			auto response = client.Post(endpoint, body, content_type);
			if (response && response->status == 200)
			{
				return response->body;
			}
			else
			{
				throw std::runtime_error("POST request failed: " +
										 (response ? std::to_string(response->status) : "no response"));
			}
		}
		else
		{
			throw std::runtime_error("Unsupported method: " + method);
		}
	}
	catch (const std::exception &e)
	{
		spdlog::error("Client request failed: {}", e.what());
		throw;
	}
}

bool Client::testConnection(int /*timeout_seconds*/)
{
	try
	{
		auto response = client.Get("/health");
		return response && response->status == 200;
	}
	catch (...)
	{
		return false;
	}
}