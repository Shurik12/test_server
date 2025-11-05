#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <server/Protocol.h>

class Config
{
public:
    static bool loadFromFile(const std::string &filename = "config.yaml");
    static bool loadFromArgs(int argc, char *argv[]);
    static std::string getString(const std::string &key, const std::string &defaultValue = "");
    static int getInt(const std::string &key, int defaultValue = 0);
    static bool getBool(const std::string &key, bool defaultValue = false);
    static bool isLoaded() { return instance_ != nullptr; }
    static std::string toString();

    // Simple protocol configuration
    static Protocol getProtocol();
    static int getPort();

private:
    Config() = default;
    bool parseYAML(const std::string &filename);
    void parseArgs(int argc, char *argv[]);

    static std::unique_ptr<Config> instance_;
    std::unordered_map<std::string, std::string> config_;
};