#pragma once

#include <cstdint>

typedef unsigned short PLAYERID;
typedef unsigned short VEHICLEID;
typedef unsigned short OBJECTID;

#define GAMESTATE_WAIT_CONNECT        1
#define GAMESTATE_CONNECTING          2
#define GAMESTATE_CONNECTED           5
#define GAMESTATE_AWAIT_JOIN          6
#define GAMESTATE_RESTARTING          11

#define MAX_PLAYER_NAME               24
#define MAX_PLAYERS                   1004
#define MAX_VEHICLES                  2000
#define MAX_OBJECTS                   2100
#define MAX_ACTORS                    1000
#define MAX_PICKUPS                   4096
#define MAX_TEXT_LABELS               2048
#define MAX_TEXT_DRAWS                2304
#define MAX_GANG_ZONES                1024
#define MAX_MAP_ICONS                 100

#define INVALID_PLAYER_ID             0xFFFF
#define INVALID_VEHICLE_ID            0xFFFF
#define INVALID_OBJECT_ID             0xFFFF
#define NO_TEAM                       0xFF
