#include <gtest/gtest.h>
#include <future>
#include <vector>
#include <string>

#include <server/RequestHandler.h>

class RequestHandlerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		handler = std::make_unique<RequestHandler>();
	}

	std::string generateValidUserJson(int id = 1, int number = 42)
	{
		return R"({"id": )" + std::to_string(id) +
			   R"(, "name": "Test User", "phone": "+1234567890", "number": )" +
			   std::to_string(number) + "}";
	}

	std::unique_ptr<RequestHandler> handler;
};

TEST_F(RequestHandlerTest, ProcessValidRequest)
{
	auto response = handler->processRequest(generateValidUserJson());
	EXPECT_TRUE(response.find("\"success\":true") != std::string::npos);
}

TEST_F(RequestHandlerTest, ProcessInvalidJson)
{
	auto response = handler->processRequest(R"({"invalid": "json")");
	EXPECT_TRUE(response.find("\"success\":false") != std::string::npos);
}

TEST_F(RequestHandlerTest, ProcessEmptyRequest)
{
	auto response = handler->processRequest("");
	EXPECT_TRUE(response.find("\"success\":false") != std::string::npos);
}

TEST_F(RequestHandlerTest, ProcessAsyncRequest)
{
	auto future = handler->processRequestAsync(generateValidUserJson(2));
	auto response = future.get();
	EXPECT_TRUE(response.find("\"success\":true") != std::string::npos);
}

TEST_F(RequestHandlerTest, StatisticsTracking)
{
	handler->resetStatistics();

	handler->processRequest(generateValidUserJson());
	handler->processRequest("invalid");

	EXPECT_EQ(handler->getRequestsProcessed(), 2);
	EXPECT_EQ(handler->getSuccessfulRequests(), 1);
	EXPECT_EQ(handler->getFailedRequests(), 1);
}

TEST_F(RequestHandlerTest, ConcurrentRequests)
{
	constexpr int num_requests = 5;
	std::vector<std::future<std::string>> futures;

	for (int i = 0; i < num_requests; ++i)
	{
		futures.push_back(handler->processRequestAsync(generateValidUserJson(i)));
	}

	int success_count = 0;
	for (auto &future : futures)
	{
		if (future.get().find("\"success\":true") != std::string::npos)
		{
			success_count++;
		}
	}

	EXPECT_EQ(success_count, num_requests);
}