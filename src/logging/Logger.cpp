
#include <vector>
#include <memory>
#include <iostream>

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <logging/Logger.h>

std::shared_ptr<spdlog::logger> Logger::create_logger()
{
	// Create async logger with thread pool
	spdlog::init_thread_pool(8192, 1);

	// Create sinks (console and file)
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/service.log", true);

	// Set individual sink levels if needed
	console_sink->set_level(spdlog::level::info);
	file_sink->set_level(spdlog::level::debug);

	// Combine sinks
	std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};

	// Create async logger
	auto logger = std::make_shared<spdlog::async_logger>(
		"service_logger",
		sinks.begin(),
		sinks.end(),
		spdlog::thread_pool(),
		spdlog::async_overflow_policy::block);

	// Set logging pattern
	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");

	// Set global logging level
	logger->set_level(spdlog::level::debug);

	return logger;
}

void Logger::initialize()
{
	try
	{
		auto logger = create_logger();
		spdlog::set_default_logger(logger);

		// Ensure logs are flushed on important levels
		spdlog::flush_on(spdlog::level::warn);

		spdlog::info("Logger initialized successfully");
		spdlog::debug("Debug logging enabled");
	}
	catch (const spdlog::spdlog_ex &ex)
	{
		std::cerr << "Log initialization failed: " << ex.what() << std::endl;
		throw;
	}
}

void Logger::shutdown()
{
	spdlog::info("Shutting down logger");
	try
	{
		spdlog::shutdown();
	}
	catch (const spdlog::spdlog_ex &ex)
	{
		std::cerr << "Log shutdown failed: " << ex.what() << std::endl;
	}
}