#pragma once
#include <stdbool.h>
#include <stdint.h>
#define TILE_COUNT 36
#define TRAY_CAP 7
typedef struct {
    uint8_t type;
    bool removed;
} tile_t;
typedef struct {
    tile_t tiles[TILE_COUNT], previous[TILE_COUNT];
    uint8_t tray[TRAY_CAP], old_tray[TRAY_CAP], solution[TILE_COUNT];
    int count, old_count, matches, old_matches, shuffles;
    bool won, lost, undo_ready, undo_used;
    uint32_t rng;
} tiles_game_t;
void tiles_init(tiles_game_t *g, uint32_t seed);
void tile_position(int i, int *x, int *y);
bool tile_available(const tiles_game_t *g, int i);
bool tiles_pick(tiles_game_t *g, int i);
bool tiles_undo(tiles_game_t *g);
bool tiles_shuffle(tiles_game_t *g);
#define SHOTS 40
#define ENEMIES 8
#define DROPS 5
#define HOSTILE 12
#define EFFECTS 8
typedef struct {
    float x, y, vx, vy;
    int hp, type;
    bool active;
} actor_t;
typedef struct {
    float x, y, fire, spawn, invulnerable, elapsed;
    actor_t shots[SHOTS], enemies[ENEMIES], drops[DROPS], hostile[HOSTILE], effects[EFFECTS];
    int score, kills, lives, weapon, power;
    bool over;
    uint32_t rng;
} shooter_t;
void shooter_init(shooter_t *g, uint32_t seed);
void shooter_tick(shooter_t *g, float roll, float pitch, float dt, int sensitivity);
