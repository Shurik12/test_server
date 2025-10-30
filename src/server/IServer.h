#pragma once

#include <string>
#include <atomic>
#include <memory>

class IServer
{
public:
	virtual ~IServer() = default;

	virtual bool start() = 0;
	virtual void stop() = 0;
	virtual bool run() = 0;

	virtual bool isRunning() const noexcept = 0;
	virtual bool isReady() const noexcept = 0;

	virtual const std::string &getHost() const noexcept = 0;
	virtual int getPort() const noexcept = 0;
	virtual std::string getAddress() const = 0;

	virtual std::string getType() const = 0;
};