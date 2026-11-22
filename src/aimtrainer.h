#ifndef AIMTRAINER_H
#define AIMTRAINER_H

#include <stdbool.h>
#include <stddef.h>

#define AIMTRAINER_MAX_TARGETS 40
bool aimtrainer_start(void);
void aimtrainer_stop(void);
bool aimtrainer_active(void);
void aimtrainer_tick(float dt);
bool aimtrainer_command(const char* text);
void aimtrainer_update_clock(void);
bool aimtrainer_can_fire(void);
void aimtrainer_damage(int id, int damage);
void aimtrainer_record_shot(bool hit);
bool aimtrainer_status(char* text, size_t size);

#endif
