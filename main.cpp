#include <iostream>
#include <string>

#include <server/Server.h>

int main(int argc, char *argv[])
{
    // Parse command line arguments
    std::string host = "0.0.0.0";
    int port = 8080;

    if (argc >= 2)
    {
        host = argv[1];
    }
    if (argc >= 3)
    {
        port = std::stoi(argv[2]);
    }

    std::cout << "C++ JSON Processing Service" << std::endl;
    std::cout << "===========================" << std::endl;
    std::cout << "Starting server on " << host << ":" << port << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;
    std::cout << std::endl;

    try
    {
        // Create and run server
        Server server(host, port);

        if (server.run())
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