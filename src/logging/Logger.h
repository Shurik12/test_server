#pragma once

#include <spdlog/spdlog.h>
#include <memory>

class Logger
{
public:
	static void initialize();
	static void shutdown();

	// Using spdlog::format_string_t which should work with any spdlog version
	template <typename... Args>
	static void trace(spdlog::format_string_t<Args...> fmt, Args &&...args)
	{
		spdlog::trace(fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	static void debug(spdlog::format_string_t<Args...> fmt, Args &&...args)
	{
		spdlog::debug(fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	static void info(spdlog::format_string_t<Args...> fmt, Args &&...args)
	{
		spdlog::info(fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	static void warn(spdlog::format_string_t<Args...> fmt, Args &&...args)
	{
		spdlog::warn(fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	static void error(spdlog::format_string_t<Args...> fmt, Args &&...args)
	{
		spdlog::error(fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	static void critical(spdlog::format_string_t<Args...> fmt, Args &&...args)
	{
		spdlog::critical(fmt, std::forward<Args>(args)...);
	}

private:
	static std::shared_ptr<spdlog::logger> create_logger();
};