#include <thread>
#include <chrono>
#include <memory>
#include <iostream>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <server/Server.h>

class TestEnvironment : public ::testing::Environment
{
public:
	void SetUp() override
	{
		spdlog::set_level(spdlog::level::warn);
		startTestServer();
	}

	void TearDown() override
	{
		if (test_server_ && test_server_->isRunning())
		{
			test_server_->stop();
		}
	}

private:
	void startTestServer()
	{
		try
		{
			test_server_ = std::make_unique<Server>("127.0.0.1", 8081);

			if (test_server_->start())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			}
		}
		catch (const std::exception &e)
		{
			std::cout << "Failed to start test server: " << e.what() << "\n";
		}
	}

	std::unique_ptr<Server> test_server_;
};

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	::testing::AddGlobalTestEnvironment(new TestEnvironment());
	return RUN_ALL_TESTS();
}