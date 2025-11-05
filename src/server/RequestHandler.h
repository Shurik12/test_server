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

	std::string processRequest(const std::string &json_input);
	std::future<std::string> processRequestAsync(const std::string &json_input);
	std::vector<std::string> processBatchRequests(const std::vector<std::string> &json_inputs);

	long long getTotalNumbersSum() const { return total_numbers_sum_; }

	long long getClientNumbersSum(const std::string &client_id)
	{
		std::lock_guard<std::mutex> lock(client_mutex_);
		auto it = client_numbers_sum_.find(client_id);
		return it != client_numbers_sum_.end() ? it->second : 0;
	}

	std::unordered_map<std::string, long long> getAllClientSums()
	{
		std::lock_guard<std::mutex> lock(client_mutex_);
		return client_numbers_sum_;
	}

	void resetNumberTracking()
	{
		total_numbers_sum_ = 0;
		std::lock_guard<std::mutex> lock(client_mutex_);
		client_numbers_sum_.clear();
	}

	// Statistics
	size_t getRequestsProcessed() const { return requests_processed_; }
	size_t getSuccessfulRequests() const { return successful_requests_; }
	size_t getFailedRequests() const { return failed_requests_; }

	// Reset statistics
	void resetStatistics();

	// Make these methods public so they can be used by MultiplexingServer
	UserData parseJson(const std::string &json_input);
	bool validateUserData(const UserData &data);
	int increase(int number);

private:
	friend class RequestHandlerTest;

	std::atomic<size_t> requests_processed_{0};
	std::atomic<size_t> successful_requests_{0};
	std::atomic<size_t> failed_requests_{0};
	std::atomic<long long> total_numbers_sum_{0};
	std::unordered_map<std::string, long long> client_numbers_sum_;
	std::mutex client_mutex_;

	std::string generateJsonResponse(const UserData &data);
	std::string generateErrorResponse(const std::string &error_message);
	std::string processRequestInternal(const std::string &json_input);
};