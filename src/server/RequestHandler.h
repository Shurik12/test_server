#pragma once

#include <string>
#include <future>
#include <functional>
#include <atomic>

#include <common/rapidjson/document.h>
#include <common/rapidjson/stringbuffer.h>
#include <common/rapidjson/writer.h>

struct UserData
{
	int id;
	std::string name;
	std::string phone;
	int number;

	// Constructor for easy initialization
	UserData(int id = 0, std::string name = "", std::string phone = "", int number = 0)
		: id(id), name(std::move(name)), phone(std::move(phone)), number(number) {}
};

class RequestHandler
{
public:
	RequestHandler();
	~RequestHandler();

	// Synchronous request processing
	std::string processRequest(const std::string &json_input);

	// Asynchronous request processing
	std::future<std::string> processRequestAsync(const std::string &json_input);

	// Batch processing for multiple requests
	std::vector<std::string> processBatchRequests(const std::vector<std::string> &json_inputs);

	// Statistics
	size_t getRequestsProcessed() const { return requests_processed_; }
	size_t getSuccessfulRequests() const { return successful_requests_; }
	size_t getFailedRequests() const { return failed_requests_; }

	// Reset statistics
	void resetStatistics();

private:
	friend class RequestHandlerTest;
	// Parse JSON input
	UserData parseJson(const std::string &json_input);

	// Validate user data
	bool validateUserData(const UserData &data);

	// Generate JSON response
	std::string generateJsonResponse(const UserData &data);

	// Generate error response
	std::string generateErrorResponse(const std::string &error_message);

	// Calculation function
	int increase(int number);

	// Process the actual request (used by async method)
	std::string processRequestInternal(const std::string &json_input);

	// Statistics
	std::atomic<size_t> requests_processed_{0};
	std::atomic<size_t> successful_requests_{0};
	std::atomic<size_t> failed_requests_{0};
};