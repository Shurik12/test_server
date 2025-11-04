#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

#include <config/Config.h>

std::unique_ptr<Config> Config::instance_ = nullptr;

bool Config::loadFromFile(const std::string &filename)
{
	if (!instance_)
		instance_.reset(new Config());

	return instance_->parseYAML(filename);
}

bool Config::loadFromArgs(int argc, char *argv[])
{
	if (!instance_)
		instance_.reset(new Config());
	instance_->parseArgs(argc, argv);
	return true;
}

std::string Config::getString(const std::string &key, const std::string &defaultValue)
{
	if (!instance_)
		return defaultValue;

	auto it = instance_->config_.find(key);
	if (it != instance_->config_.end())
		return it->second;

	return defaultValue;
}

int Config::getInt(const std::string &key, int defaultValue)
{
	if (!instance_)
	{
		return defaultValue;
	}

	auto it = instance_->config_.find(key);
	if (it != instance_->config_.end())
	{
		try
		{
			return std::stoi(it->second);
		}
		catch (const std::exception &)
		{
			return defaultValue;
		}
	}

	return defaultValue;
}

bool Config::getBool(const std::string &key, bool defaultValue)
{
	if (!instance_)
	{
		return defaultValue;
	}

	auto it = instance_->config_.find(key);
	if (it != instance_->config_.end())
	{
		std::string value = it->second;
		std::transform(value.begin(), value.end(), value.begin(), ::tolower);
		return (value == "true" || value == "1" || value == "yes" || value == "on");
	}

	return defaultValue;
}

std::string Config::toString()
{
	if (!instance_)
	{
		return "Configuration not loaded";
	}

	std::stringstream ss;
	ss << "Configuration:\n";
	for (const auto &[key, value] : instance_->config_)
	{
		ss << "  " << key << ": " << value << "\n";
	}
	return ss.str();
}

bool Config::parseYAML(const std::string &filename)
{
	std::ifstream file(filename);
	if (!file.is_open())
	{
		std::cerr << "Warning: Could not open config file: " << filename << std::endl;
		return false;
	}

	std::string line;
	std::string current_section;

	while (std::getline(file, line))
	{
		// Remove comments
		size_t comment_pos = line.find('#');
		if (comment_pos != std::string::npos)
		{
			line = line.substr(0, comment_pos);
		}

		// Trim whitespace
		line.erase(0, line.find_first_not_of(" \t"));
		line.erase(line.find_last_not_of(" \t") + 1);

		if (line.empty())
			continue;

		// Check for section
		if (line.back() == ':')
		{
			current_section = line.substr(0, line.length() - 1);
			continue;
		}

		// Parse key-value pair
		size_t colon_pos = line.find(':');
		if (colon_pos != std::string::npos)
		{
			std::string key = line.substr(0, colon_pos);
			std::string value = line.substr(colon_pos + 1);

			// Trim whitespace from key and value
			key.erase(0, key.find_first_not_of(" \t"));
			key.erase(key.find_last_not_of(" \t") + 1);
			value.erase(0, value.find_first_not_of(" \t"));
			value.erase(value.find_last_not_of(" \t") + 1);

			// Remove quotes if present
			if (!value.empty() && value.front() == '"' && value.back() == '"')
			{
				value = value.substr(1, value.length() - 2);
			}

			// Build full key with section
			std::string full_key = current_section.empty() ? key : current_section + "." + key;
			instance_->config_[full_key] = value;
		}
	}

	std::cout << "Configuration loaded from: " << filename << std::endl;
	return true;
}

void Config::parseArgs(int argc, char *argv[])
{
	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];

		if (arg.find("--") == 0)
		{
			// Remove leading --
			arg = arg.substr(2);

			size_t equals_pos = arg.find('=');
			if (equals_pos != std::string::npos)
			{
				std::string key = arg.substr(0, equals_pos);
				std::string value = arg.substr(equals_pos + 1);
				instance_->config_[key] = value;
			}
			else
			{
				// Boolean flag
				instance_->config_[arg] = "true";
			}
		}
		else if (i == 1 && arg.find('-') != 0)
		{
			// First argument without -- is host
			instance_->config_["server.host"] = arg;
		}
		else if (i == 2 && arg.find('-') != 0)
		{
			// Second argument without -- is port
			instance_->config_["server.port"] = arg;
		}
	}
}

void Config::flattenMap(const std::string &prefix, const std::unordered_map<std::string, std::string> &map)
{
	for (const auto &[key, value] : map)
	{
		std::string full_key = prefix.empty() ? key : prefix + "." + key;
		instance_->config_[full_key] = value;
	}
}

std::vector<Protocol> Config::getEnabledProtocols()
{
	if (!instance_)
	{
		return {Protocol::HTTP}; // Default to HTTP only
	}

	std::vector<Protocol> enabledProtocols;

	// Check each protocol individually
	if (getBool("protocols.tcp", false))
	{
		enabledProtocols.push_back(Protocol::TCP);
	}
	if (getBool("protocols.udp", false))
	{
		enabledProtocols.push_back(Protocol::UDP);
	}
	if (getBool("protocols.sctp", false))
	{
		enabledProtocols.push_back(Protocol::SCTP);
	}
	if (getBool("protocols.http", true)) // HTTP enabled by default
	{
		enabledProtocols.push_back(Protocol::HTTP);
	}

	// If none specified, default to HTTP
	if (enabledProtocols.empty())
	{
		enabledProtocols.push_back(Protocol::HTTP);
	}

	return enabledProtocols;
}

bool Config::isProtocolEnabled(Protocol protocol)
{
	if (!instance_)
	{
		return protocol == Protocol::HTTP; // Default to HTTP only
	}

	switch (protocol)
	{
	case Protocol::TCP:
		return getBool("protocols.tcp", false);
	case Protocol::UDP:
		return getBool("protocols.udp", false);
	case Protocol::SCTP:
		return getBool("protocols.sctp", false);
	case Protocol::HTTP:
		return getBool("protocols.http", true);
	default:
		return false;
	}
}

bool Config::isProtocolEnabled(const std::string &protocolName)
{
	Protocol protocol = ProtocolFactory::stringToProtocol(protocolName);
	return isProtocolEnabled(protocol);
}