#ifndef AIMTRAINER_H
#define AIMTRAINER_H

#include <stdbool.h>
#include <stddef.h>

#define AIMTRAINER_MAX_TARGETS 40
#define AIMTRAINER_MAP_SIZE 512
#define AIMTRAINER_CENTER (AIMTRAINER_MAP_SIZE / 2)
#define AIMTRAINER_GROUND 4
#define AIMTRAINER_PLATFORM_RADIUS 2
#define AIMTRAINER_MOAT_WIDTH 5

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
/* Chebyshev rings give a 5-block gap on all four sides of the platform. */
int aimtrainer_column_distance(int x, int z);

#endif
