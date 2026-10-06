#include "core/arcade.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    for (unsigned seed = 0; seed < 100; seed++) {
        tiles_game_t g;
        tiles_init(&g, seed);
        assert(!tile_available(&g, 0));
        for (int n = 0; n < TILE_COUNT; n++) {
            assert(tile_available(&g, g.solution[n]));
            assert(tiles_pick(&g, g.solution[n]));
            assert(g.count < 3);
        }
        assert(g.won && !g.lost && g.matches == 12 && g.count == 0);
        assert(tiles_undo(&g));
        assert(!g.won && g.count == 2);
        assert(!tiles_undo(&g));
        assert(tiles_pick(&g, g.solution[35]) && g.won);
    }
    tiles_game_t g;
    tiles_init(&g, 55);
    for (int i = 24; i < 31; i++)
        g.tiles[i].type = i - 24;
    for (int i = 24; i < 31; i++)
        assert(tiles_pick(&g, i));
    assert(g.lost && g.count == 7);
    assert(!tiles_pick(&g, 31));
    assert(tiles_undo(&g) && !g.lost && g.count == 6);
    tiles_init(&g, 10);
    int counts[9] = {0};
    for (int i = 0; i < TILE_COUNT; i++)
        counts[g.tiles[i].type]++;
    assert(tiles_shuffle(&g));
    assert(tiles_shuffle(&g));
    assert(!tiles_shuffle(&g));
    for (int i = 0; i < TILE_COUNT; i++)
        counts[g.tiles[i].type]--;
    for (int i = 0; i < 9; i++)
        assert(!counts[i]);
    shooter_t s;
    shooter_init(&s, 1);
    shooter_tick(&s, 100000, 100000, .05f, 5);
    assert(s.x <= ARENA_WIDTH - 16 && s.y <= ARENA_HEIGHT - 17);
    for (int i = 0; i < 100; i++)
        shooter_tick(&s, 100000, 100000, .05f, 5);
    assert(s.x == ARENA_WIDTH - 16 && s.y == ARENA_HEIGHT - 17);
    s.over = false;
    s.lives = 100;
    for (int i = 0; i < 100; i++)
        shooter_tick(&s, -100000, -100000, .05f, 5);
    assert(s.x == 16 && s.y == 20);
    shooter_init(&s, 1);
    s.drops[0] = (actor_t){s.x, s.y, 0, 0, 1, 1, true};
    shooter_tick(&s, 0, 0, .01f, 3);
    assert(s.weapon == 1 && s.power == 1 && !s.drops[0].active);
    for (int i = 0; i < 4; i++) {
        s.drops[0] = (actor_t){s.x, s.y, 0, 0, 1, 1, true};
        shooter_tick(&s, 0, 0, .01f, 3);
    }
    assert(s.power == 3);
    s.hostile[0] = (actor_t){s.x, s.y, 0, 0, 1, 0, true};
    shooter_tick(&s, 0, 0, .01f, 3);
    assert(s.lives == 2);
    s.hostile[1] = (actor_t){s.x, s.y, 0, 0, 1, 0, true};
    shooter_tick(&s, 0, 0, .01f, 3);
    assert(s.lives == 2);
    s.invulnerable = 0;
    s.lives = 1;
    shooter_tick(&s, 0, 0, .01f, 3);
    assert(s.over);
    shooter_init(&s, 9);
    s.fire = 100;
    s.spawn = 100;
    s.kills = 2;
    s.enemies[0] = (actor_t){150, 70, 0, 0, 1, 0, true};
    s.shots[0] = (actor_t){150, 70, 0, 0, 1, 0, true};
    shooter_tick(&s, 0, 0, .01f, 3);
    assert(s.kills == 3 && s.score == 100 && s.drops[0].active);
    puts("Tile solvability, blocking, tray, undo, shuffle and shooter bounds/combat/upgrades PASS");
}
