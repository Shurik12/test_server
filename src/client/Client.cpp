#include <client/Client.h>

Client::Client(const std::string &host, int port)
	: host(host), port(port), client(host, port)
{
	client.set_connection_timeout(10);
	client.set_read_timeout(30);
}

std::string Client::sendRequest(const std::string &endpoint, const std::string &method,
								const std::string &body, const std::string &content_type)
{
	if (method == "GET")
	{
		auto response = client.Get(endpoint);
		return response ? response->body : "No response";
	}
	else if (method == "POST")
	{
		auto response = client.Post(endpoint, body, content_type);
		return response ? response->body : "No response";
	}

	return "Unsupported method: " + method;
}