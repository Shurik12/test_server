#include <iostream>
#include <string>
#include <memory>

#include <config/Config.h>
#include <server/ServerFactory.h> // Use the factory

int main(int argc, char *argv[])
{
    // Load configuration
    Config::loadFromFile("config.yaml");
    Config::loadFromArgs(argc, argv);

    // Get server configuration with defaults
    std::string host = Config::getString("server.host", "0.0.0.0");
    int port = Config::getInt("server.port", 8080);
    std::string server_type = Config::getString("server.type", "blocking");

    std::cout << "C++ JSON Processing Service" << std::endl;
    std::cout << "===========================" << std::endl;
    std::cout << "Starting " << server_type << " server on " << host << ":" << port << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;
    std::cout << std::endl;

    try
    {
        // Create and run server using factory
        auto server = ServerFactory::createFromConfig();

        if (server->run())
        {
            std::cout << "Server stopped gracefully" << std::endl;
            return 0;
        }
        else
        {
            std::cerr << "Server failed to start or encountered an error" << std::endl;
            return 1;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}