#pragma once

#include <string>
#include <chrono>

#include <common/httplib.h>

class Client
{
public:
	Client(const std::string &host = "localhost", int port = 8080);

	// Core request method used by tests
	std::string sendRequest(const std::string &endpoint, const std::string &method,
							const std::string &body = "", const std::string &content_type = "application/json");

private:
	std::string host;
	int port;
	httplib::Client client;
};