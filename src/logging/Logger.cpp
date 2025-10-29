#include <vector>
#include <memory>
#include <iostream>

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <config/Config.h>
#include <logging/Logger.h>

// Helper function to convert string to spdlog level
spdlog::level::level_enum string_to_level(const std::string &level_str)
{
	if (level_str == "trace")
		return spdlog::level::trace;
	if (level_str == "debug")
		return spdlog::level::debug;
	if (level_str == "info")
		return spdlog::level::info;
	if (level_str == "warn")
		return spdlog::level::warn;
	if (level_str == "error")
		return spdlog::level::err;
	if (level_str == "critical")
		return spdlog::level::critical;
	return spdlog::level::info; // default
}

std::shared_ptr<spdlog::logger> Logger::create_logger()
{
	// Get configuration values
	std::string log_level = Config::getString("logging.level", "debug");
	std::string log_file = Config::getString("logging.file", "logs/service.log");
	std::string log_pattern = Config::getString("logging.pattern", "[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
	std::string flush_level = Config::getString("logging.flush_on", "warn");

	// Create async logger with thread pool
	spdlog::init_thread_pool(8192, 1);

	// Create sinks (console and file)
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);

	// Set individual sink levels
	console_sink->set_level(string_to_level(log_level));
	file_sink->set_level(spdlog::level::debug); // File always gets debug and above

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
	logger->set_pattern(log_pattern);

	// Set global logging level
	logger->set_level(string_to_level(log_level));

	return logger;
}

void Logger::initialize()
{
	try
	{
		auto logger = create_logger();
		spdlog::set_default_logger(logger);

		// Ensure logs are flushed on important levels
		std::string flush_level = Config::getString("logging.flush_on", "warn");
		spdlog::flush_on(string_to_level(flush_level));

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