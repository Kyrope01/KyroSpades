#include <stdbool.h>

#include "stdio.h"
#include "enet/enet.h"

struct Demo {
	FILE* fp;
	float start_time;
};

FILE* create_demo_file();
void register_demo_packet(ENetPacket *packet);
void demo_start_record();
void demo_stop_record();
bool demo_is_server_omited_packet();