#include "game.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool valid_player(int player) {
    return player >= 0 && player < GAME_PLAYER_COUNT;
}

void game_reset(GameState *game) {
    for (int player = 0; player < GAME_PLAYER_COUNT; ++player) {
        game->life[player] = GAME_STARTING_LIFE;
        game->poison[player] = 0;
        game->eliminated[player] = false;
        for (int source = 0; source < GAME_PLAYER_COUNT; ++source) {
            game->commander_damage[player][source] = 0;
        }
    }
    game->first_player = GAME_NO_PLAYER;
    game->monarch_player = GAME_NO_PLAYER;
}

static bool player_meets_loss_threshold(const GameState *game, int player) {
    if (!game || !valid_player(player)) return false;
    if (game->life[player] <= 0 || game->poison[player] >= 10) return true;
    for (int source = 0; source < GAME_PLAYER_COUNT; ++source) {
        if (game->commander_damage[player][source] >= 21) return true;
    }
    return false;
}

bool game_evaluate_elimination_after_mutation(GameState *game, int player) {
    if (!player_meets_loss_threshold(game, player) ||
        game->eliminated[player]) return false;
    game->eliminated[player] = true;
    return true;
}

bool game_revive(GameState *game, int player) {
    if (!game || !valid_player(player) || !game->eliminated[player]) return false;
    if (game->poison[player] == 10) game->poison[player] = 9;
    if (game->life[player] <= 0) game->life[player] = 1;
    game->eliminated[player] = false;
    return true;
}

void game_adjust(GameState *game, int player, int delta) {
    if (!valid_player(player)) return;
    if (delta > 0 && game->life[player] > INT_MAX - delta) return;
    if (delta < 0 && game->life[player] < INT_MIN - delta) return;
    game->life[player] += delta;
}

bool game_add_commander_damage(GameState *game, int recipient, int source) {
    if (!valid_player(recipient) || !valid_player(source)) return false;
    if (game->commander_damage[recipient][source] == INT_MAX ||
        game->life[recipient] == INT_MIN) return false;
    ++game->commander_damage[recipient][source];
    --game->life[recipient];
    return true;
}

bool game_clear_commander_damage(GameState *game, int recipient, int source) {
    if (!valid_player(recipient) || !valid_player(source)) return false;
    int damage = game->commander_damage[recipient][source];
    if (damage <= 0 || game->life[recipient] > INT_MAX - damage) return false;
    game->life[recipient] += damage;
    game->commander_damage[recipient][source] = 0;
    return true;
}

bool game_apply_tap(GameState *game, GameControl control) {
    if (!valid_player(control.player)) return false;
    if (control.type == GAME_CONTROL_LIFE) {
        if (control.delta != -1 && control.delta != 1) return false;
        int previous = game->life[control.player];
        game_adjust(game, control.player, control.delta);
        return game->life[control.player] != previous;
    }
    if (control.type == GAME_CONTROL_COMMANDER) {
        return game_add_commander_damage(game, control.player, control.source);
    }
    if (control.type == GAME_CONTROL_POISON) {
        if (game->poison[control.player] == INT_MAX) return false;
        ++game->poison[control.player];
        return true;
    }
    if (control.type == GAME_CONTROL_MONARCH) {
        if (game->monarch_player == control.player) return false;
        game->monarch_player = control.player;
        return true;
    }
    if (control.type == GAME_CONTROL_REVIVE) {
        return game_revive(game, control.player);
    }
    return false;
}

bool game_apply_hold_from_snapshot(GameState *game, const GameState *before,
                                   GameControl control) {
    if (!game || !before || !valid_player(control.player)) return false;

    if (control.type == GAME_CONTROL_PLAYER_STATE) {
        bool target = !before->eliminated[control.player];
        if (game->eliminated[control.player] == target) return false;
        game->eliminated[control.player] = target;
        return true;
    }
    if (control.type == GAME_CONTROL_LIFE) {
        if (control.delta != -1 && control.delta != 1) return false;
        long long target = (long long)before->life[control.player] +
                           (long long)control.delta * GAME_LONG_PRESS_LIFE_DELTA;
        if (target < INT_MIN || target > INT_MAX ||
            game->life[control.player] == (int)target) return false;
        game->life[control.player] = (int)target;
        return true;
    }
    if (control.type == GAME_CONTROL_COMMANDER) {
        if (!valid_player(control.source)) return false;
        int damage = before->commander_damage[control.player][control.source];
        long long target_life = (long long)before->life[control.player] + damage;
        if (damage < 0 || target_life < INT_MIN || target_life > INT_MAX) return false;
        bool changed = game->life[control.player] != (int)target_life ||
                       game->commander_damage[control.player][control.source] != 0;
        game->life[control.player] = (int)target_life;
        game->commander_damage[control.player][control.source] = 0;
        return changed;
    }
    if (control.type == GAME_CONTROL_POISON) {
        if (game->poison[control.player] == 0) return false;
        game->poison[control.player] = 0;
        return true;
    }
    if (control.type == GAME_CONTROL_MONARCH) {
        int target = before->monarch_player == control.player
            ? GAME_NO_PLAYER : before->monarch_player;
        if (game->monarch_player == target) return false;
        game->monarch_player = target;
        return true;
    }
    return false;
}

bool game_control_changed_from_snapshot(const GameState *game,
                                        const GameState *before,
                                        GameControl control) {
    if (!game || !before || !valid_player(control.player)) return false;
    if (control.type == GAME_CONTROL_LIFE) {
        return game->life[control.player] != before->life[control.player];
    }
    if (control.type == GAME_CONTROL_COMMANDER) {
        return valid_player(control.source) &&
               (game->life[control.player] != before->life[control.player] ||
                game->commander_damage[control.player][control.source] !=
                    before->commander_damage[control.player][control.source]);
    }
    if (control.type == GAME_CONTROL_POISON) {
        return game->poison[control.player] != before->poison[control.player];
    }
    if (control.type == GAME_CONTROL_MONARCH) {
        return game->monarch_player != before->monarch_player;
    }
    if (control.type == GAME_CONTROL_PLAYER_STATE) {
        return game->eliminated[control.player] !=
               before->eliminated[control.player];
    }
    return false;
}

bool game_control_equal(GameControl left, GameControl right) {
    return left.type == right.type && left.player == right.player &&
           left.source == right.source && left.delta == right.delta;
}

void game_set_first(GameState *game, int player) {
    if (valid_player(player) || player == GAME_NO_PLAYER) game->first_player = player;
}

void game_life_preview_reset(GameLifePreview *preview) {
    if (!preview) return;
    memset(preview, 0, sizeof(*preview));
}

bool game_life_preview_clear_player(GameLifePreview *preview, int player) {
    if (!preview || !valid_player(player)) return false;
    bool changed = preview->visible[player] || preview->net[player] != 0 ||
                   preview->expires_at_ms[player] != 0;
    preview->visible[player] = false;
    preview->net[player] = 0;
    preview->expires_at_ms[player] = 0;
    return changed;
}

bool game_life_preview_record(GameLifePreview *preview, int player,
                              int life_before, int life_after,
                              int64_t now_ms) {
    if (!preview || !valid_player(player)) return false;
    if (preview->visible[player] &&
        now_ms >= preview->expires_at_ms[player]) {
        (void)game_life_preview_clear_player(preview, player);
    }
    int64_t delta = (int64_t)life_after - (int64_t)life_before;
    if (delta == 0) return false;
    if (delta > 0 && preview->net[player] > INT64_MAX - delta) {
        preview->net[player] = INT64_MAX;
    } else if (delta < 0 && preview->net[player] < INT64_MIN - delta) {
        preview->net[player] = INT64_MIN;
    } else {
        preview->net[player] += delta;
    }
    if (preview->net[player] == 0) {
        game_life_preview_clear_player(preview, player);
        return true;
    }
    preview->visible[player] = true;
    preview->expires_at_ms[player] =
        now_ms > INT64_MAX - GAME_LIFE_PREVIEW_DURATION_MS
            ? INT64_MAX
            : now_ms + GAME_LIFE_PREVIEW_DURATION_MS;
    return true;
}

bool game_life_preview_expire(GameLifePreview *preview, int64_t now_ms) {
    if (!preview) return false;
    bool changed = false;
    for (int player = 0; player < GAME_PLAYER_COUNT; ++player) {
        if (preview->visible[player] &&
            now_ms >= preview->expires_at_ms[player]) {
            changed = game_life_preview_clear_player(preview, player) || changed;
        }
    }
    return changed;
}

bool game_life_preview_format(const GameLifePreview *preview, int player,
                              char *text, size_t text_size) {
    if (!preview || !valid_player(player) || !text || text_size < 4 ||
        !preview->visible[player] || preview->net[player] == 0) return false;
    char sign = preview->net[player] < 0 ? '-' : '+';
    int magnitude;
    if (preview->net[player] > 99 || preview->net[player] < -99) {
        magnitude = 99;
    } else {
        magnitude = preview->net[player] < 0
            ? (int)-preview->net[player]
            : (int)preview->net[player];
    }
    return snprintf(text, text_size, "%c%02d", sign, magnitude) == 3;
}

static bool parse_line(char *line, char *key, size_t key_size, int *value) {
    char *separator = strchr(line, '=');
    if (!separator || separator == line) return false;
    *separator = '\0';
    if (strlen(line) >= key_size) return false;
    strcpy(key, line);

    errno = 0;
    char *end = NULL;
    long parsed = strtol(separator + 1, &end, 10);
    if (errno == ERANGE || end == separator + 1 || parsed < INT_MIN ||
        parsed > INT_MAX) return false;
    while (*end == '\n' || *end == '\r') ++end;
    if (*end != '\0') return false;
    *value = (int)parsed;
    return true;
}

bool game_load(GameState *game, const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) return false;

    GameState loaded;
    game_reset(&loaded);
    bool life_seen[GAME_PLAYER_COUNT] = {false};
    bool commander_seen[GAME_PLAYER_COUNT][GAME_PLAYER_COUNT] = {{false}};
    bool poison_seen[GAME_PLAYER_COUNT] = {false};
    bool eliminated_seen[GAME_PLAYER_COUNT] = {false};
    bool version_seen = false;
    int version = 0;
    bool first_seen = false;
    bool monarch_seen = false;
    bool valid = true;
    char line[128];
    while (valid && fgets(line, sizeof(line), file)) {
        char key[16];
        int value;
        if (!parse_line(line, key, sizeof(key), &value)) {
            valid = false;
        } else if (strcmp(key, "VERSION") == 0 && !version_seen &&
                   (value == 2 || value == 3 || value == 4)) {
            version_seen = true;
            version = value;
        } else if (key[0] == 'P' && key[1] >= '1' && key[1] <= '4' &&
                   key[2] == '\0') {
            int index = key[1] - '1';
            if (life_seen[index]) {
                valid = false;
            } else {
                loaded.life[index] = value;
                life_seen[index] = true;
            }
        } else if (key[0] == 'C' && key[1] == 'D' &&
                   key[2] >= '1' && key[2] <= '4' &&
                   key[3] >= '1' && key[3] <= '4' && key[4] == '\0') {
            int recipient = key[2] - '1';
            int source = key[3] - '1';
            if (value < 0 || commander_seen[recipient][source]) {
                valid = false;
            } else {
                loaded.commander_damage[recipient][source] = value;
                commander_seen[recipient][source] = true;
            }
        } else if (strncmp(key, "POISON", 6) == 0 &&
                   key[6] >= '1' && key[6] <= '4' && key[7] == '\0') {
            int player = key[6] - '1';
            if (value < 0 || poison_seen[player]) {
                valid = false;
            } else {
                loaded.poison[player] = value;
                poison_seen[player] = true;
            }
        } else if (strncmp(key, "ELIM", 4) == 0 &&
                   key[4] >= '1' && key[4] <= '4' && key[5] == '\0') {
            int player = key[4] - '1';
            if ((value != 0 && value != 1) || eliminated_seen[player]) {
                valid = false;
            } else {
                loaded.eliminated[player] = value == 1;
                eliminated_seen[player] = true;
            }
        } else if (strcmp(key, "FIRST") == 0 && !first_seen &&
                   value >= 0 && value <= GAME_PLAYER_COUNT) {
            loaded.first_player = value - 1;
            first_seen = true;
        } else if (strcmp(key, "MONARCH") == 0 && !monarch_seen &&
                   value >= 0 && value <= GAME_PLAYER_COUNT) {
            loaded.monarch_player = value - 1;
            monarch_seen = true;
        } else if ((strcmp(key, "INITIATIVE") == 0 &&
                    value >= 0 && value <= GAME_PLAYER_COUNT) ||
                   (key[0] == 'U' && key[1] == 'C' &&
                    key[2] >= '1' && key[2] <= '4' && key[3] == '\0' &&
                    value >= 0 && value <= 8)) {
            /* Saved by the removed initiative tracker. Keep the rest of the game. */
        } else {
            valid = false;
        }
    }
    if (ferror(file)) valid = false;
    if (fclose(file) != 0) valid = false;

    for (int i = 0; i < GAME_PLAYER_COUNT; ++i) {
        if (!life_seen[i]) valid = false;
    }
    if (version_seen) {
        if (!first_seen || !monarch_seen) valid = false;
        for (int player = 0; player < GAME_PLAYER_COUNT; ++player) {
            if (!poison_seen[player]) valid = false;
            if (version == 3 && !eliminated_seen[player]) valid = false;
            if (version == 2 && eliminated_seen[player]) valid = false;
            for (int source = 0; source < GAME_PLAYER_COUNT; ++source) {
                if (!commander_seen[player][source]) valid = false;
            }
        }
    } else {
        for (int player = 0; player < GAME_PLAYER_COUNT; ++player) {
            if (poison_seen[player] || eliminated_seen[player]) valid = false;
            for (int source = 0; source < GAME_PLAYER_COUNT; ++source) {
                if (commander_seen[player][source]) valid = false;
            }
        }
    }
    if (valid) *game = loaded;
    return valid;
}

bool game_save(const GameState *game, const char *path) {
    if (!path) return false;
    size_t temp_size = strlen(path) + 5;
    char *temp_path = malloc(temp_size);
    if (!temp_path) return false;
    snprintf(temp_path, temp_size, "%s.tmp", path);

    FILE *file = fopen(temp_path, "w");
    if (!file) {
        free(temp_path);
        return false;
    }

    bool ok = fprintf(file, "VERSION=3\nP1=%d\nP2=%d\nP3=%d\nP4=%d\n",
                      game->life[0], game->life[1], game->life[2],
                      game->life[3]) > 0;
    for (int recipient = 0; ok && recipient < GAME_PLAYER_COUNT; ++recipient) {
        for (int source = 0; ok && source < GAME_PLAYER_COUNT; ++source) {
            ok = fprintf(file, "CD%d%d=%d\n", recipient + 1, source + 1,
                         game->commander_damage[recipient][source]) > 0;
        }
    }
    for (int player = 0; ok && player < GAME_PLAYER_COUNT; ++player) {
        ok = fprintf(file, "POISON%d=%d\n", player + 1,
                     game->poison[player]) > 0;
    }
    for (int player = 0; ok && player < GAME_PLAYER_COUNT; ++player) {
        ok = fprintf(file, "ELIM%d=%d\n", player + 1,
                     game->eliminated[player] ? 1 : 0) > 0;
    }
    if (ok) {
        ok = fprintf(file, "FIRST=%d\nMONARCH=%d\n",
                     game->first_player + 1,
                     game->monarch_player + 1) > 0;
    }
    if (ok && fflush(file) != 0) ok = false;
    if (ok && fsync(fileno(file)) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    if (ok && rename(temp_path, path) != 0) ok = false;
    if (!ok) unlink(temp_path);
    free(temp_path);
    return ok;
}
