#ifndef MTG_LIFE_COUNTER_GAME_H
#define MTG_LIFE_COUNTER_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GAME_PLAYER_COUNT 4
#define GAME_STARTING_LIFE 40
#define GAME_LONG_PRESS_LIFE_DELTA 10
#define GAME_NO_PLAYER (-1)
#define GAME_LIFE_PREVIEW_DURATION_MS 3000

typedef enum {
    GAME_CONTROL_NONE,
    GAME_CONTROL_LIFE,
    GAME_CONTROL_COMMANDER,
    GAME_CONTROL_POISON,
    GAME_CONTROL_MONARCH,
    GAME_CONTROL_PLAYER_STATE,
} GameControlType;

typedef struct {
    GameControlType type;
    int player;
    int source;
    int delta;
} GameControl;

typedef struct {
    int life[GAME_PLAYER_COUNT];
    int commander_damage[GAME_PLAYER_COUNT][GAME_PLAYER_COUNT];
    int poison[GAME_PLAYER_COUNT];
    bool eliminated[GAME_PLAYER_COUNT];
    int first_player;
    int monarch_player;
} GameState;

typedef struct {
    int64_t net[GAME_PLAYER_COUNT];
    int64_t expires_at_ms[GAME_PLAYER_COUNT];
    bool visible[GAME_PLAYER_COUNT];
} GameLifePreview;

void game_reset(GameState *game);
void game_adjust(GameState *game, int player, int delta);
bool game_add_commander_damage(GameState *game, int recipient, int source);
bool game_clear_commander_damage(GameState *game, int recipient, int source);
bool game_apply_tap(GameState *game, GameControl control);
bool game_apply_hold_from_snapshot(GameState *game, const GameState *before,
                                   GameControl control);
bool game_control_changed_from_snapshot(const GameState *game,
                                        const GameState *before,
                                        GameControl control);
bool game_evaluate_elimination_after_mutation(GameState *game, int player);
bool game_control_equal(GameControl left, GameControl right);
void game_life_preview_reset(GameLifePreview *preview);
bool game_life_preview_record(GameLifePreview *preview, int player,
                              int life_before, int life_after,
                              int64_t now_ms);
bool game_life_preview_clear_player(GameLifePreview *preview, int player);
bool game_life_preview_expire(GameLifePreview *preview, int64_t now_ms);
bool game_life_preview_format(const GameLifePreview *preview, int player,
                              char *text, size_t text_size);
void game_set_first(GameState *game, int player);
bool game_load(GameState *game, const char *path);
bool game_save(const GameState *game, const char *path);

#endif
