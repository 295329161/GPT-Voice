#include "arcade.h"
#include "tilt.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static uint32_t random_next(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s;
}
void tile_position(int i, int *x, int *y) {
    int z = i / 12, n = i % 12;
    *x = 10 + (n % 6) * 46 + z * 10;
    *y = 10 + (n / 6) * 52 + z * 16;
}
bool tile_available(const tiles_game_t *g, int i) {
    if (i < 0 || i >= TILE_COUNT || g->tiles[i].removed)
        return false;
    int x, y;
    tile_position(i, &x, &y);
    for (int j = (i / 12 + 1) * 12; j < TILE_COUNT; j++) {
        int a, b;
        tile_position(j, &a, &b);
        if (!g->tiles[j].removed && abs(x - a) < 42 && abs(y - b) < 45)
            return false;
    }
    return true;
}
void tiles_init(tiles_game_t *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->rng = seed;
    g->shuffles = 2;
    // Construct a legal removal order first, then assign triples along it.
    for (int n = 0; n < TILE_COUNT; n++) {
        int available[TILE_COUNT], count = 0;
        for (int i = 0; i < TILE_COUNT; i++)
            if (tile_available(g, i))
                available[count++] = i;
        int i = available[random_next(&g->rng) % count];
        g->solution[n] = i;
        g->tiles[i].removed = true;
    }
    for (int n = 0; n < TILE_COUNT; n += 3) {
        int type = (n / 3) % 9;
        for (int k = 0; k < 3; k++) {
            int i = g->solution[n + k];
            g->tiles[i].type = type;
            g->tiles[i].removed = false;
        }
    }
}
bool tiles_pick(tiles_game_t *g, int i) {
    if (g->won || g->lost || !tile_available(g, i))
        return false;
    memcpy(g->previous, g->tiles, sizeof(g->tiles));
    memcpy(g->old_tray, g->tray, sizeof(g->tray));
    g->old_count = g->count;
    g->old_matches = g->matches;
    g->undo_ready = true;
    int type = g->tiles[i].type;
    g->tiles[i].removed = true;
    g->tray[g->count++] = type;
    int equal = 0;
    for (int n = 0; n < g->count; n++)
        if (g->tray[n] == type)
            equal++;
    if (equal == 3) {
        int count = 0;
        for (int n = 0; n < g->count; n++)
            if (g->tray[n] != type)
                g->tray[count++] = g->tray[n];
        g->count = count;
        g->matches++;
    }
    g->won = g->matches == TILE_COUNT / 3;
    g->lost = g->count == TRAY_CAP;
    return true;
}
bool tiles_undo(tiles_game_t *g) {
    if (!g->undo_ready || g->undo_used)
        return false;
    memcpy(g->tiles, g->previous, sizeof(g->tiles));
    memcpy(g->tray, g->old_tray, sizeof(g->tray));
    g->count = g->old_count;
    g->matches = g->old_matches;
    g->lost = g->won = false;
    g->undo_ready = false;
    g->undo_used = true;
    return true;
}
bool tiles_shuffle(tiles_game_t *g) {
    if (g->lost || g->won || !g->shuffles)
        return false;
    int ids[TILE_COUNT], n = 0;
    for (int i = 0; i < TILE_COUNT; i++)
        if (!g->tiles[i].removed)
            ids[n++] = i;
    for (int i = n - 1; i > 0; i--) {
        int j = random_next(&g->rng) % (i + 1);
        uint8_t t = g->tiles[ids[i]].type;
        g->tiles[ids[i]].type = g->tiles[ids[j]].type;
        g->tiles[ids[j]].type = t;
    }
    g->shuffles--;
    g->undo_ready = false;
    return true;
}
void shooter_init(shooter_t *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->rng = seed;
    g->x = ARENA_WIDTH / 2;
    g->y = ARENA_HEIGHT - 32;
    g->lives = 3;
    g->power = 1;
    g->spawn = .4f;
}
static void spawn(actor_t *a, int n, float x, float y, float vx, float vy, int hp, int type) {
    for (int i = 0; i < n; i++)
        if (!a[i].active) {
            a[i] = (actor_t){x, y, vx, vy, hp, type, true};
            return;
        }
}
static bool hit(float x, float y, float a, float b, float radius) {
    return fabsf(x - a) < radius && fabsf(y - b) < radius;
}
static float clamp(float v, float low, float high) {
    return fmaxf(low, fminf(high, v));
}
void shooter_tick(shooter_t *g, float roll, float pitch, float dt, int sensitivity) {
    if (g->over)
        return;
    dt = clamp(dt, 0, .05f);
    g->elapsed += dt;
    g->x = clamp(g->x + tilt_speed(roll, sensitivity) * dt, 16, ARENA_WIDTH - 16);
    g->y = clamp(g->y + tilt_speed(pitch, sensitivity) * dt, 20, ARENA_HEIGHT - 17);
    g->invulnerable = fmaxf(0, g->invulnerable - dt);
    g->fire -= dt;
    g->spawn -= dt;
    if (g->fire <= 0) {
        g->fire = g->weapon == 1 ? .14f : .22f;
        int n = g->weapon == 0 ? g->power * 2 - 1 : g->weapon == 2 ? 2 * g->power : 1;
        for (int k = 0; k < n; k++) {
            float offset = k - (n - 1) * .5f;
            spawn(g->shots, SHOTS, g->x + (g->weapon == 2 ? offset * 20 : offset * 4), g->y - 12,
                  g->weapon == 0 ? offset * 32 : 0, -190, g->weapon == 1 ? g->power + 1 : 1,
                  g->weapon);
        }
    }
    if (g->spawn <= 0) {
        int wave = g->kills / 12;
        g->spawn = fmaxf(.38f, 1.05f - wave * .1f);
        int type = random_next(&g->rng) % 3;
        spawn(g->enemies, ENEMIES, 20 + random_next(&g->rng) % 260, -14, type == 1 ? 25 : 0,
              23 + wave * 4, type == 2 ? 3 : 1, type);
    }
    for (int i = 0; i < SHOTS; i++)
        if (g->shots[i].active) {
            actor_t *a = &g->shots[i];
            a->x += a->vx * dt;
            a->y += a->vy * dt;
            if (a->y < -12 || a->x < 0 || a->x > ARENA_WIDTH)
                a->active = false;
        }
    for (int i = 0; i < ENEMIES; i++)
        if (g->enemies[i].active) {
            actor_t *a = &g->enemies[i];
            float old = a->y;
            a->x += a->vx * dt;
            a->y += a->vy * dt;
            if (a->x < 16 || a->x > ARENA_WIDTH - 16)
                a->vx = -a->vx;
            if (old < 45 && a->y >= 45)
                spawn(g->hostile, HOSTILE, a->x, a->y, 0, 55, 1, 0);
            for (int k = 0; k < SHOTS; k++)
                if (g->shots[k].active && hit(a->x, a->y, g->shots[k].x, g->shots[k].y, 13)) {
                    a->hp -= g->shots[k].hp;
                    g->shots[k].active = false;
                    if (a->hp <= 0) {
                        a->active = false;
                        g->score += 100;
                        spawn(g->effects, EFFECTS, a->x, a->y, 0, .35f, 1, 0);
                        g->kills++;
                        if (g->kills % 3 == 0)
                            spawn(g->drops, DROPS, a->x, a->y, 0, 26, 1, random_next(&g->rng) % 4);
                        break;
                    }
                }
            if (a->active && hit(g->x, g->y, a->x, a->y, 18) && g->invulnerable <= 0) {
                g->lives--;
                g->invulnerable = 1.5f;
                a->active = false;
            }
            if (a->y > ARENA_HEIGHT + 20)
                a->active = false;
        }
    for (int i = 0; i < HOSTILE; i++)
        if (g->hostile[i].active) {
            actor_t *a = &g->hostile[i];
            a->y += a->vy * dt;
            if (a->y > ARENA_HEIGHT + 10)
                a->active = false;
            if (hit(g->x, g->y, a->x, a->y, 10) && g->invulnerable <= 0) {
                g->lives--;
                g->invulnerable = 1.5f;
                a->active = false;
            }
        }
    for (int i = 0; i < DROPS; i++)
        if (g->drops[i].active) {
            actor_t *a = &g->drops[i];
            a->y += a->vy * dt;
            if (a->y > ARENA_HEIGHT + 15)
                a->active = false;
            if (hit(g->x, g->y, a->x, a->y, 23)) {
                a->active = false;
                if (a->type == 3) {
                    if (g->lives < 3)
                        g->lives++;
                    g->invulnerable = 2;
                } else {
                    g->power = g->weapon == a->type ? (g->power < 3 ? g->power + 1 : 3) : 1;
                    g->weapon = a->type;
                }
                g->score += 25;
            }
        }
    for (int i = 0; i < EFFECTS; i++)
        if (g->effects[i].active) {
            g->effects[i].vy -= dt;
            if (g->effects[i].vy <= 0)
                g->effects[i].active = false;
        }
    if (g->lives <= 0)
        g->over = true;
}
