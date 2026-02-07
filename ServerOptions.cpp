// ServerOptions.cpp
#include "ServerOptions.h"
#include <iostream>
#include <cstdlib>

static bool IsFlag(const char* arg, const char* name)
{
    return std::string(arg) == name;
}

ServerOptions ParseCommandLine(int argc, char* argv[])
{
    ServerOptions opt;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--port" && i + 1 < argc)
        {
            opt.port = std::atoi(argv[++i]);
        }
        else if (arg == "--max-players" && i + 1 < argc)
        {
            opt.maxPlayers = std::atoi(argv[++i]);
        }
        else if (arg == "--tickrate" && i + 1 < argc)
        {
            opt.tickRate = std::atoi(argv[++i]);
        }
        else if (arg == "--config" && i + 1 < argc)
        {
            opt.config = argv[++i];
        }
        else if (arg == "--dev")
        {
            opt.devMode = true;
        }
        else if (arg == "--no-persistence")
        {
            opt.noPersistence = true;
        }
        else if (arg == "--no-mobjitter")
        {
            opt.noMobJitter = true;
        }
        else if (arg == "--showfullstate")
        {
            opt.showFullState = true;
        }
        else if (arg == "--help")
        {
            std::cout <<
                "Dara Server Options:\n"
                "  --port <n>            Server port (default 8080)\n"
                "  --max-players <n>     (not implemented) Max players (default 8)\n"
                "  --tickrate <n>        (not implemented) Game tickrate (default 20)\n"
                "  --config <file>       (not implemented) Config file\n"
                "  --dev                 (not implemented) Enable dev mode\n"
                "  --no-persistence      (not implemented) Disable DB persistence\n"
                "  --no-mobjitter        Mobs x pos will not be random each turn\n"
                "  --showfullstate    Each turn and player the full state reply will be sent\n"
                "  --help                Show this help\n";
            std::exit(0);
        }
    }

    return opt;
}
