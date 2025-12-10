#include <cxxopts.hpp>
#include <iostream>
#include "myfs.h"
#include "server.h"

int main(int argc, char *argv[])
{
    MyFSOptions options;
    try
    {
        options.parse(argc, argv);
    }
    catch (const std::exception &e)
    {
        if (std::string(e.what()) == "help")
        {
            std::cout << options.opts.help() << std::endl;
            return 0;
        }
        else
        {
            std::cerr << "Error: " << e.what() << std::endl;
            std::cerr << options.opts.help() << std::endl;
            return 1;
        }
    }

    if (options.is_server) return server_main(options);
    else return myfs_main(options, argc, argv);
}