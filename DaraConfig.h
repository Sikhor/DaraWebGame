#pragma once
inline constexpr int WEBSERVER_PORT= 9050;
inline constexpr int PLAYERS_EXPECTED= 1;
inline constexpr int PLAYERS_MAX= 1;

// Debug Flags
inline constexpr int DARA_DEBUG_MESSAGES= 0;
inline constexpr int DARA_DEBUG_NEWMESSAGES= 0;
inline constexpr int DARA_DEBUG_ATTACKS= 0;
inline constexpr int DARA_DEBUG_AI_ACTIONS= 0;
inline constexpr int DARA_DEBUG_AI_REPLIES= 0;
inline constexpr int DARA_DEBUG_MSGSTATS= 0;
inline constexpr int DARA_DEBUG_PLAYERSTATS= 0; 
inline constexpr int DARA_DEBUG_MOBSTATS= 0; 
inline constexpr int DARA_DEBUG_MOBCOMBAT= 0; 
inline constexpr int DARA_DEBUG_COMBAT= 0;
inline constexpr int DARA_DEBUG_STORYLOG= 0;
inline constexpr int DARA_DEBUG_SPAWNS= 0;
inline constexpr int DARA_DEBUG_FULLSTATE= 0;


inline constexpr int DARA_MAX_MOBS= 4;
inline constexpr int DARA_TURN_TIMEOUT= 3000;
inline constexpr int DARA_GAMEOVER_PAUSE=10000;


inline constexpr std::string_view DARA_DEAD_AVATAR_PLAYER = "Death";
inline constexpr std::string_view DARA_DEAD_AVATAR_MOB= "Death";
inline constexpr std::string_view DARA_MOB_STORE= "mobs/mobdb.json";


inline constexpr float DARA_MOB_SPEED= 0.1f;
