/*
Demo recording compatible with aos_replay (https://github.com/BR-/aos_replay   )
*/

#include "stdio.h"
#include "demo.h"
#include "time.h"
#include "string.h"
#include "sys/stat.h"
#include "enet/enet.h"
#include "window.h"
#include "network.h"

struct Demo CurrentDemo;
static const struct Demo ResetStruct;

FILE* create_demo_file() {
	char file_name[128];
	char dir_path[] = "demos";
	
	// Create demos directory if it doesn't exist
	mkdir(dir_path, 0755);
	
	time_t demo_time;
	time(&demo_time);
	struct tm* tm_info = localtime(&demo_time);
	snprintf(file_name, sizeof(file_name), "%s/%04d-%02d-%02d_%02d-%02d-%02d.demo", dir_path, 
		tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
		tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

	FILE* file;
	file = fopen(file_name, "wb");
	
	if (file == NULL) {
		log_info("Demo failed to create file");
		return NULL;
	}
	
	// aos_replay version + 0.75 version
	unsigned char value = 1;
	fwrite(&value, sizeof(value), 1, file);

	value = 3;
	fwrite(&value, sizeof(value), 1, file);	

	return file;
}

void register_demo_packet(ENetPacket *packet) {
	if (!CurrentDemo.fp)
		return;

	float c_time = window_time()-CurrentDemo.start_time;
	unsigned short len = packet->dataLength;

	fwrite(&c_time, sizeof(c_time), 1, CurrentDemo.fp);
	fwrite(&len, sizeof(len), 1, CurrentDemo.fp);

	fwrite(packet->data, packet->dataLength, 1, CurrentDemo.fp);
}


void demo_start_record() {
	CurrentDemo.fp = create_demo_file();
	CurrentDemo.start_time = window_time();
	log_info("Demo Recording started.");
}

void demo_stop_record() {
	if(CurrentDemo.fp)
		fclose(CurrentDemo.fp);

	CurrentDemo = ResetStruct;
	log_info("Demo Recording ended.");
}

bool demo_is_server_omited_packet(int id) {
	int omited_ids[] = { 
		PACKET_INPUTDATA_ID, PACKET_WEAPONINPUT_ID, PACKET_SETTOOL_ID, 
		PACKET_SETCOLOR_ID, PACKET_WEAPONRELOAD_ID, 
	};
	for (int i = 0; i < sizeof(omited_ids) / sizeof(int); i++)
		if (omited_ids[i] == id)
			return true;
	return false;
}