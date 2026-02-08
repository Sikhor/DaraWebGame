// ServerOptions.h
#pragma once
#include <string>

struct ServerOptions
{
    int port            = 9050;
    int maxPlayers      = 8;
    int tickRate        = 20;      // game ticks per second
    bool devMode        = false;
    bool noPersistence  = false;
    bool noMobJitter    = false;
    bool showFullState  = false;
    bool showLeaderBoards = false;
    std::string config  = "server.json";
};

extern ServerOptions g_options;