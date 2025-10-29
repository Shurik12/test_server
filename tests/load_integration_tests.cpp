#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <algorithm>
#include <client/Client.h>

class IntegrationTest : public ::testing::Test
{
protected:
	static constexpr int TEST_PORT = 8081;

	void SetUp() override
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		if (!isServerReady())
		{
			GTEST_SKIP() << "Test server not available";
		}
	}

	bool isServerReady(int max_attempts = 3)
	{
		for (int i = 0; i < max_attempts; ++i)
		{
			try
			{
				Client client("127.0.0.1", TEST_PORT);
				auto response = client.sendRequest("/health", "GET");
				return response.find("success") != std::string::npos ||
					   response.find("healthy") != std::string::npos;
			}
			catch (...)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
		}
		return false;
	}

	std::vector<bool> runLoadTest(int num_clients, int requests_per_client)
	{
		std::vector<std::thread> threads;
		std::vector<bool> results;
		std::mutex results_mutex;

		auto worker = [&](int client_id)
		{
			try
			{
				Client client("127.0.0.1", TEST_PORT);
				std::string json_data = generateTestData(client_id);

				for (int i = 0; i < requests_per_client; ++i)
				{
					try
					{
						auto response = client.sendRequest("/process", "POST", json_data);
						std::lock_guard lock(results_mutex);
						results.push_back(response.find("\"success\":true") != std::string::npos);
					}
					catch (...)
					{
						std::lock_guard lock(results_mutex);
						results.push_back(false);
					}
				}
			}
			catch (...)
			{
				std::lock_guard lock(results_mutex);
				results.insert(results.end(), requests_per_client, false);
			}
		};

		for (int i = 0; i < num_clients; ++i)
		{
			threads.emplace_back(worker, i);
		}

		for (auto &thread : threads)
		{
			thread.join();
		}

		return results;
	}

private:
	std::string generateTestData(int client_id) const
	{
		return R"({"id": )" + std::to_string(client_id) +
			   R"(, "name": "Client )" + std::to_string(client_id) +
			   R"(", "phone": "+1-555-)" + std::to_string(1000 + client_id) +
			   R"(", "number": )" + std::to_string(client_id * 10) + "}";
	}
};

TEST_F(IntegrationTest, ServerHealth)
{
	Client client("127.0.0.1", TEST_PORT);
	auto response = client.sendRequest("/health", "GET");
	EXPECT_TRUE(response.find("success") != std::string::npos ||
				response.find("healthy") != std::string::npos);
}

TEST_F(IntegrationTest, BasicFunctionality)
{
	Client client("127.0.0.1", TEST_PORT);
	auto response = client.sendRequest("/process", "POST",
									   R"({"id": 123, "name": "Test", "phone": "+1234567890", "number": 42})");

	EXPECT_TRUE(response.find("success") != std::string::npos);
}

TEST_F(IntegrationTest, LightLoad)
{
	auto results = runLoadTest(3, 3);
	int successful = std::count(results.begin(), results.end(), true);
	EXPECT_GE(successful, static_cast<int>(results.size() * 0.95));
}

TEST_F(IntegrationTest, MediumLoad)
{
	auto results = runLoadTest(5, 5);
	int successful = std::count(results.begin(), results.end(), true);
	EXPECT_GE(successful, static_cast<int>(results.size() * 0.90));
}

TEST_F(IntegrationTest, MultipleEndpoints)
{
	Client client("127.0.0.1", TEST_PORT);

	auto root_response = client.sendRequest("/", "GET");
	EXPECT_FALSE(root_response.empty());

	auto metrics_response = client.sendRequest("/metrics", "GET");
	EXPECT_TRUE(metrics_response.find("{") != std::string::npos);

	auto async_response = client.sendRequest("/process-async", "POST",
											 R"({"id": 999, "name": "Test", "phone": "+9999999999", "number": 100})");
	EXPECT_TRUE(async_response.find("success") != std::string::npos);
}