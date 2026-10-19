#include "game.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_reset_and_adjust(void) {
    GameState game;
    game_reset(&game);
    for (int i = 0; i < GAME_PLAYER_COUNT; ++i) assert(game.life[i] == 40);
    assert(game.first_player == GAME_NO_PLAYER);
    assert(game.monarch_player == GAME_NO_PLAYER);

    game_adjust(&game, 0, -1);
    game_adjust(&game, 3, 1);
    assert(game.life[0] == 39);
    assert(game.life[3] == 41);
}

static void test_reset_clears_v2_trackers(void) {
    GameState game;
    memset(&game, 0x7f, sizeof(game));

    game_reset(&game);

    for (int recipient = 0; recipient < GAME_PLAYER_COUNT; ++recipient) {
        assert(game.poison[recipient] == 0);
        for (int source = 0; source < GAME_PLAYER_COUNT; ++source) {
            assert(game.commander_damage[recipient][source] == 0);
        }
    }
}

static void test_reset_and_manual_player_state_hold_preserve_underlying_game(void) {
    GameState game;
    game_reset(&game);
    for (int player = 0; player < GAME_PLAYER_COUNT; ++player) {
        assert(!game.eliminated[player]);
    }
    game.life[2] = 17;
    game.poison[2] = 4;
    game.commander_damage[2][1] = 8;
    game.first_player = 2;
    game.monarch_player = 2;
    GameControl player_state = {
        .type = GAME_CONTROL_PLAYER_STATE,
        .player = 2,
        .source = GAME_NO_PLAYER,
        .delta = 0,
    };

    GameState before = game;
    assert(!game_apply_tap(&game, player_state));
    assert(game_apply_hold_from_snapshot(&game, &before, player_state));
    assert(game.eliminated[2]);
    assert(game.life[2] == 17);
    assert(game.poison[2] == 4);
    assert(game.commander_damage[2][1] == 8);
    assert(game.first_player == 2);
    assert(game.monarch_player == 2);

    before = game;
    assert(game_apply_hold_from_snapshot(&game, &before, player_state));
    assert(!game.eliminated[2]);
    assert(game.life[2] == 17);

    game_reset(&game);
    assert(!game.eliminated[2]);
}

static void test_life_threshold_eliminates_and_manual_restore_waits_for_mutation(void) {
    GameState game;
    game_reset(&game);
    game.life[0] = 1;
    GameControl minus = {
        .type = GAME_CONTROL_LIFE,
        .player = 0,
        .source = GAME_NO_PLAYER,
        .delta = -1,
    };
    GameControl player_state = {
        .type = GAME_CONTROL_PLAYER_STATE,
        .player = 0,
        .source = GAME_NO_PLAYER,
        .delta = 0,
    };

    assert(game_apply_tap(&game, minus));
    assert(game.life[0] == 0);
    assert(!game.eliminated[0]);
    assert(game_evaluate_elimination_after_mutation(&game, 0));
    assert(game.eliminated[0]);

    GameState before = game;
    assert(game_apply_hold_from_snapshot(&game, &before, player_state));
    assert(!game.eliminated[0]);

    GameControl crown = {
        .type = GAME_CONTROL_MONARCH,
        .player = 0,
        .source = GAME_NO_PLAYER,
        .delta = 0,
    };
    assert(game_apply_tap(&game, crown));
    assert(!game.eliminated[0]);

    GameControl poison = {
        .type = GAME_CONTROL_POISON,
        .player = 0,
        .source = GAME_NO_PLAYER,
        .delta = 0,
    };
    assert(game_apply_tap(&game, poison));
    assert(game_evaluate_elimination_after_mutation(&game, 0));
    assert(game.eliminated[0]);
}

static void test_revive_removes_threshold_poison_and_restores_nonpositive_life(void) {
    GameState game;
    game_reset(&game);
    game.life[0] = 0;
    game.poison[0] = 10;
    game.eliminated[0] = true;
    game.life[1] = -4;
    game.poison[1] = 3;
    game.eliminated[1] = true;
    game.life[2] = 12;
    game.poison[2] = 10;
    game.eliminated[2] = true;
    game.life[3] = 8;
    game.poison[3] = 11;
    game.commander_damage[3][0] = 21;
    game.eliminated[3] = true;

    GameControl revive = {
        .type = GAME_CONTROL_REVIVE,
        .player = 0,
        .source = GAME_NO_PLAYER,
        .delta = 0,
    };
    assert(game_apply_tap(&game, revive));
    assert(game.life[0] == 1);
    assert(game.poison[0] == 9);
    assert(!game.eliminated[0]);
    assert(!game_apply_tap(&game, revive));

    revive.player = 1;
    assert(game_revive(&game, 1));
    assert(game.life[1] == 1);
    assert(game.poison[1] == 3);
    assert(!game.eliminated[1]);

    revive.player = 2;
    assert(game_revive(&game, 2));
    assert(game.life[2] == 12);
    assert(game.poison[2] == 9);
    assert(!game.eliminated[2]);

    revive.player = 3;
    assert(game_revive(&game, 3));
    assert(game.life[3] == 8);
    assert(game.poison[3] == 11);
    assert(game.commander_damage[3][0] == 21);
    assert(!game.eliminated[3]);
    assert(!game_revive(&game, 3));
}

static void test_poison_and_single_commander_thresholds_eliminate(void) {
    GameState game;
    game_reset(&game);
    GameControl poison = {
        .type = GAME_CONTROL_POISON,
        .player = 1,
        .source = GAME_NO_PLAYER,
        .delta = 0,
    };
    game.poison[1] = 9;
    assert(game_apply_tap(&game, poison));
    assert(game.poison[1] == 10);
    assert(!game.eliminated[1]);
    assert(game_evaluate_elimination_after_mutation(&game, 1));
    assert(game.eliminated[1]);

    game_reset(&game);
    game.commander_damage[3][0] = 10;
    game.commander_damage[3][1] = 10;
    GameControl commander = {
        .type = GAME_CONTROL_COMMANDER,
        .player = 3,
        .source = 1,
        .delta = 0,
    };
    assert(game_apply_tap(&game, commander));
    assert(game.commander_damage[3][1] == 11);
    assert(!game.eliminated[3]);

    game.commander_damage[3][1] = 20;
    assert(game_apply_tap(&game, commander));
    assert(game.commander_damage[3][1] == 21);
    assert(!game.eliminated[3]);
    assert(game_evaluate_elimination_after_mutation(&game, 3));
    assert(game.eliminated[3]);
}

static void test_provisional_threshold_taps_do_not_poison_final_hold_state(void) {
    GameState game;
    game_reset(&game);
    game.poison[0] = 9;
    GameControl poison = {
        .type = GAME_CONTROL_POISON,
        .player = 0,
        .source = GAME_NO_PLAYER,
        .delta = 0,
    };
    GameState before = game;
    assert(game_apply_tap(&game, poison));
    assert(game.poison[0] == 10);
    assert(!game.eliminated[0]);
    assert(game_apply_hold_from_snapshot(&game, &before, poison));
    assert(game.poison[0] == 0);
    assert(!game_evaluate_elimination_after_mutation(&game, 0));
    assert(!game.eliminated[0]);

    game_reset(&game);
    game.commander_damage[1][2] = 20;
    game.life[1] = 20;
    GameControl commander = {
        .type = GAME_CONTROL_COMMANDER,
        .player = 1,
        .source = 2,
        .delta = 0,
    };
    before = game;
    assert(game_apply_tap(&game, commander));
    assert(game.commander_damage[1][2] == 21);
    assert(game.life[1] == 19);
    assert(!game.eliminated[1]);
    assert(game_apply_hold_from_snapshot(&game, &before, commander));
    assert(game.commander_damage[1][2] == 0);
    assert(game.life[1] == 40);
    assert(!game_evaluate_elimination_after_mutation(&game, 1));
    assert(!game.eliminated[1]);
}

static void test_zero_clear_holds_are_not_final_mutations(void) {
    const GameControl controls[] = {
        {GAME_CONTROL_POISON, 0, GAME_NO_PLAYER, 1},
        {GAME_CONTROL_COMMANDER, 0, 2, 1},
    };
    for (size_t index = 0; index < sizeof(controls) / sizeof(controls[0]);
         ++index) {
        GameState game;
        game_reset(&game);
        game.life[0] = 0;
        GameState before = game;

        assert(game_apply_tap(&game, controls[index]));
        assert(game_apply_hold_from_snapshot(&game, &before, controls[index]));
        assert(!game_control_changed_from_snapshot(&game, &before,
                                                   controls[index]));
        if (game_control_changed_from_snapshot(&game, &before,
                                               controls[index])) {
            (void)game_evaluate_elimination_after_mutation(&game, 0);
        }
        assert(!game.eliminated[0]);
        assert(game.life[0] == before.life[0]);
        assert(game.poison[0] == before.poison[0]);
        assert(game.commander_damage[0][2] ==
               before.commander_damage[0][2]);
    }
}

static void test_commander_damage_tap_tracks_source_and_life(void) {
    GameState game;
    game_reset(&game);

    assert(game_add_commander_damage(&game, 1, 3));
    assert(game_add_commander_damage(&game, 1, 3));

    assert(game.commander_damage[1][3] == 2);
    assert(game.life[1] == 38);
    assert(game.commander_damage[3][1] == 0);
    assert(game.life[3] == 40);
}

static void test_commander_hold_refunds_only_that_sources_damage(void) {
    GameState game;
    game_reset(&game);
    game_adjust(&game, 0, -4);
    assert(game_add_commander_damage(&game, 0, 1));
    assert(game_add_commander_damage(&game, 0, 1));
    assert(game_add_commander_damage(&game, 0, 2));
    assert(game.life[0] == 33);

    GameControl commander = {
        .type = GAME_CONTROL_COMMANDER,
        .player = 0,
        .source = 1,
        .delta = 0,
    };
    GameState before = game;
    assert(game_apply_tap(&game, commander));
    assert(game_apply_hold_from_snapshot(&game, &before, commander));
    assert(game.commander_damage[0][1] == 0);
    assert(game.commander_damage[0][2] == 1);
    assert(game.life[0] == 35);
}

static void test_commander_damage_hold_clears_and_refunds_life(void) {
    GameState game;
    game_reset(&game);
    assert(game_add_commander_damage(&game, 2, 0));
    assert(game_add_commander_damage(&game, 2, 0));
    assert(game_add_commander_damage(&game, 2, 0));
    assert(game_add_commander_damage(&game, 2, 1));

    assert(game_clear_commander_damage(&game, 2, 0));

    assert(game.commander_damage[2][0] == 0);
    assert(game.commander_damage[2][1] == 1);
    assert(game.life[2] == 39);
    assert(!game_clear_commander_damage(&game, 2, 0));
    assert(game.life[2] == 39);
}

static void test_commander_damage_actions_are_atomic_at_integer_limits(void) {
    GameState game;
    game_reset(&game);

    game.life[0] = INT_MIN;
    assert(!game_add_commander_damage(&game, 0, 1));
    assert(game.life[0] == INT_MIN);
    assert(game.commander_damage[0][1] == 0);

    game_reset(&game);
    game.commander_damage[1][2] = INT_MAX;
    assert(!game_add_commander_damage(&game, 1, 2));
    assert(game.life[1] == 40);
    assert(game.commander_damage[1][2] == INT_MAX);

    game_reset(&game);
    game.life[3] = INT_MAX;
    game.commander_damage[3][0] = 1;
    assert(!game_clear_commander_damage(&game, 3, 0));
    assert(game.life[3] == INT_MAX);
    assert(game.commander_damage[3][0] == 1);
}

static void test_first_player_selection(void) {
    GameState game;
    game_reset(&game);
    game_set_first(&game, 2);
    assert(game.first_player == 2);
    game_set_first(&game, GAME_NO_PLAYER);
    assert(game.first_player == GAME_NO_PLAYER);
}

static void test_adjustment_does_not_overflow(void) {
    GameState game;
    game_reset(&game);
    game.life[0] = INT_MAX;
    game_adjust(&game, 0, 1);
    assert(game.life[0] == INT_MAX);
    game.life[1] = INT_MIN;
    game_adjust(&game, 1, -1);
    assert(game.life[1] == INT_MIN);
}

static void test_game_control_life_tap_and_hold_total_ten(void) {
    GameState game;
    game_reset(&game);
    GameControl plus = {
        .type = GAME_CONTROL_LIFE,
        .player = 0,
        .source = GAME_NO_PLAYER,
        .delta = 1,
    };
    GameControl minus = plus;
    minus.player = 1;
    minus.delta = -1;

    GameState before_press = game;
    assert(game_apply_tap(&game, plus));
    assert(game_apply_hold_from_snapshot(&game, &before_press, plus));
    assert(game.life[0] == 50);

    before_press = game;
    assert(game_apply_tap(&game, minus));
    assert(game_apply_hold_from_snapshot(&game, &before_press, minus));
    assert(game.life[1] == 30);
}

static void test_game_control_commander_tap_and_hold_refund(void) {
    GameState game;
    game_reset(&game);
    GameControl commander = {
        .type = GAME_CONTROL_COMMANDER,
        .player = 2,
        .source = 1,
        .delta = 0,
    };

    GameState before_press = game;
    assert(game_apply_tap(&game, commander));
    assert(game.commander_damage[2][1] == 1);
    assert(game.life[2] == 39);
    assert(game_apply_hold_from_snapshot(&game, &before_press, commander));
    assert(game.commander_damage[2][1] == 0);
    assert(game.life[2] == 40);
}

static void test_game_control_poison_taps_and_hold_clears(void) {
    GameState game;
    game_reset(&game);
    GameControl poison = {
        .type = GAME_CONTROL_POISON,
        .player = 3,
        .source = GAME_NO_PLAYER,
        .delta = 0,
    };

    assert(game_apply_tap(&game, poison));
    assert(game.poison[3] == 1);
    GameState before_press = game;
    assert(game_apply_tap(&game, poison));
    assert(game.poison[3] == 2);
    assert(game_apply_hold_from_snapshot(&game, &before_press, poison));
    assert(game.poison[3] == 0);
    before_press = game;
    assert(!game_apply_hold_from_snapshot(&game, &before_press, poison));
}

static void test_poison_tap_stops_at_integer_limit(void) {
    GameState game;
    game_reset(&game);
    game.poison[0] = INT_MAX;
    GameControl poison = {
        .type = GAME_CONTROL_POISON,
        .player = 0,
        .source = GAME_NO_PLAYER,
        .delta = 0,
    };

    assert(!game_apply_tap(&game, poison));
    assert(game.poison[0] == INT_MAX);
}

static void test_game_control_monarch_tap_assigns_and_active_hold_clears(void) {
    GameState game;
    game_reset(&game);
    GameControl crown_0 = {
        .type = GAME_CONTROL_MONARCH,
        .player = 0,
        .source = GAME_NO_PLAYER,
        .delta = 0,
    };
    GameControl crown_2 = crown_0;
    crown_2.player = 2;

    assert(game_apply_tap(&game, crown_0));
    assert(game.monarch_player == 0);
    assert(!game_apply_tap(&game, crown_0));
    assert(game.monarch_player == 0);
    assert(game_apply_tap(&game, crown_2));
    assert(game.monarch_player == 2);

    GameState before_press = game;
    assert(!game_apply_tap(&game, crown_2));
    assert(game_apply_hold_from_snapshot(&game, &before_press, crown_2));
    assert(game.monarch_player == GAME_NO_PLAYER);
}

static void test_monarch_hold_restores_press_time_owner(void) {
    GameState game;
    game_reset(&game);
    GameControl crown_0 = {
        .type = GAME_CONTROL_MONARCH,
        .player = 0,
        .source = GAME_NO_PLAYER,
        .delta = 0,
    };
    GameControl crown_2 = crown_0;
    crown_2.player = 2;

    assert(game_apply_tap(&game, crown_0));
    GameState before_press = game;
    assert(game_apply_tap(&game, crown_2));
    assert(game.monarch_player == 2);
    assert(game_apply_hold_from_snapshot(&game, &before_press, crown_2));
    assert(game.monarch_player == 0);

    before_press = game;
    assert(!game_apply_tap(&game, crown_0));
    assert(game_apply_hold_from_snapshot(&game, &before_press, crown_0));
    assert(game.monarch_player == GAME_NO_PLAYER);
}

static void test_game_control_equality_uses_every_identity_field(void) {
    GameControl base = {
        .type = GAME_CONTROL_COMMANDER,
        .player = 1,
        .source = 3,
        .delta = 0,
    };
    GameControl changed = base;
    assert(game_control_equal(base, changed));
    changed.type = GAME_CONTROL_POISON;
    assert(!game_control_equal(base, changed));
    changed = base;
    changed.player = 2;
    assert(!game_control_equal(base, changed));
    changed = base;
    changed.source = 0;
    assert(!game_control_equal(base, changed));
    changed = base;
    changed.delta = 1;
    assert(!game_control_equal(base, changed));
}

static void test_life_preview_accumulates_nets_and_independent_deadlines(void) {
    GameLifePreview preview;
    game_life_preview_reset(&preview);
    char text[8];

    assert(game_life_preview_record(&preview, 0, 40, 39, 1000));
    assert(preview.visible[0]);
    assert(preview.net[0] == -1);
    assert(preview.expires_at_ms[0] == 4000);
    assert(game_life_preview_format(&preview, 0, text, sizeof(text)));
    assert(strcmp(text, "-01") == 0);

    assert(game_life_preview_record(&preview, 0, 39, 30, 2000));
    assert(preview.net[0] == -10);
    assert(preview.expires_at_ms[0] == 5000);
    assert(game_life_preview_format(&preview, 0, text, sizeof(text)));
    assert(strcmp(text, "-10") == 0);

    assert(game_life_preview_record(&preview, 1, 40, 48, 1500));
    assert(preview.net[1] == 8);
    assert(preview.expires_at_ms[1] == 4500);
    assert(game_life_preview_format(&preview, 1, text, sizeof(text)));
    assert(strcmp(text, "+08") == 0);
    assert(preview.net[0] == -10);
    assert(preview.expires_at_ms[0] == 5000);

    assert(game_life_preview_record(&preview, 0, 30, 40, 2500));
    assert(!preview.visible[0]);
    assert(preview.net[0] == 0);
    assert(preview.expires_at_ms[0] == 0);
    assert(!game_life_preview_format(&preview, 0, text, sizeof(text)));
}

static void test_life_preview_expiry_clear_and_visual_clamp_preserve_true_net(void) {
    GameLifePreview preview;
    game_life_preview_reset(&preview);
    char text[8];

    assert(game_life_preview_record(&preview, 2, 0, INT_MAX, 10));
    assert(preview.net[2] == INT_MAX);
    assert(game_life_preview_format(&preview, 2, text, sizeof(text)));
    assert(strcmp(text, "+99") == 0);

    assert(game_life_preview_record(&preview, 2, INT_MAX, 98, 20));
    assert(preview.net[2] == 98);
    assert(game_life_preview_format(&preview, 2, text, sizeof(text)));
    assert(strcmp(text, "+98") == 0);

    assert(!game_life_preview_expire(&preview, 3019));
    assert(preview.visible[2]);
    assert(game_life_preview_expire(&preview, 3020));
    assert(!preview.visible[2]);
    assert(preview.net[2] == 0);

    assert(game_life_preview_record(&preview, 3, 0, INT_MIN, 100));
    assert(game_life_preview_format(&preview, 3, text, sizeof(text)));
    assert(strcmp(text, "-99") == 0);
    assert(game_life_preview_clear_player(&preview, 3));
    assert(!game_life_preview_clear_player(&preview, 3));
    assert(!preview.visible[3]);

    assert(game_life_preview_record(&preview, 0, 40, 45, 1000));
    assert(preview.net[0] == 5);
    assert(game_life_preview_record(&preview, 0, 45, 46, 5000));
    assert(preview.net[0] == 1);
    assert(preview.expires_at_ms[0] == 8000);
}

static void test_state_round_trip(void) {
    char path[] = "/tmp/mtg-game-test-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    GameState written;
    game_reset(&written);
    written.life[0] = 35;
    written.life[2] = 47;
    written.commander_damage[0][3] = 7;
    written.commander_damage[2][1] = 21;
    written.poison[1] = 4;
    written.poison[3] = 10;
    written.eliminated[2] = true;
    written.eliminated[3] = true;
    written.first_player = 3;
    written.monarch_player = 1;
    assert(game_save(&written, path));

    GameState loaded;
    assert(game_load(&loaded, path));
    assert(memcmp(written.life, loaded.life, sizeof(written.life)) == 0);
    assert(memcmp(written.commander_damage, loaded.commander_damage,
                  sizeof(written.commander_damage)) == 0);
    assert(memcmp(written.poison, loaded.poison, sizeof(written.poison)) == 0);
    assert(memcmp(written.eliminated, loaded.eliminated,
                  sizeof(written.eliminated)) == 0);
    assert(loaded.first_player == 3);
    assert(loaded.monarch_player == 1);
    unlink(path);
}

static void test_saved_state_declares_version_three(void) {
    char path[] = "/tmp/mtg-game-version-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    GameState game;
    game_reset(&game);
    assert(game_save(&game, path));

    FILE *file = fopen(path, "r");
    assert(file);
    char line[32];
    assert(fgets(line, sizeof(line), file));
    assert(strcmp(line, "VERSION=3\n") == 0);
    assert(fclose(file) == 0);
    unlink(path);
}

static void test_v2_state_migrates_with_players_active(void) {
    char path[] = "/tmp/mtg-game-v2-migrate-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    assert(fputs("VERSION=2\nP1=0\nP2=40\nP3=12\nP4=5\n", file) >= 0);
    for (int recipient = 0; recipient < GAME_PLAYER_COUNT; ++recipient) {
        for (int source = 0; source < GAME_PLAYER_COUNT; ++source) {
            assert(fprintf(file, "CD%d%d=%d\n", recipient + 1, source + 1,
                           recipient == 2 && source == 1 ? 21 : 0) > 0);
        }
    }
    for (int player = 0; player < GAME_PLAYER_COUNT; ++player) {
        assert(fprintf(file, "POISON%d=%d\n", player + 1,
                       player == 3 ? 10 : 0) > 0);
    }
    assert(fputs("FIRST=2\nMONARCH=4\n", file) >= 0);
    assert(fclose(file) == 0);

    GameState game;
    assert(game_load(&game, path));
    assert(game.life[0] == 0);
    assert(game.commander_damage[2][1] == 21);
    assert(game.poison[3] == 10);
    for (int player = 0; player < GAME_PLAYER_COUNT; ++player) {
        assert(!game.eliminated[player]);
    }
    unlink(path);
}

static void rewrite_elim2_value(const char *path, int value) {
    char replacement[] = "/tmp/mtg-game-v3-rewrite-XXXXXX";
    int fd = mkstemp(replacement);
    assert(fd >= 0);
    FILE *input = fopen(path, "r");
    FILE *output = fdopen(fd, "w");
    assert(input && output);
    char line[128];
    bool replaced = false;
    while (fgets(line, sizeof(line), input)) {
        if (strncmp(line, "ELIM2=", 6) == 0) {
            assert(fprintf(output, "ELIM2=%d\n", value) > 0);
            replaced = true;
        } else {
            assert(fputs(line, output) >= 0);
        }
    }
    assert(replaced);
    assert(fclose(input) == 0);
    assert(fclose(output) == 0);
    assert(rename(replacement, path) == 0);
}

static void test_v3_rejects_duplicate_and_non_boolean_elimination(void) {
    char path[] = "/tmp/mtg-game-v3-invalid-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    GameState saved;
    game_reset(&saved);
    saved.eliminated[1] = true;
    assert(game_save(&saved, path));

    FILE *file = fopen(path, "a");
    assert(file);
    assert(fputs("ELIM1=0\n", file) >= 0);
    assert(fclose(file) == 0);
    GameState target;
    game_reset(&target);
    target.life[0] = 777;
    assert(!game_load(&target, path));
    assert(target.life[0] == 777);

    assert(game_save(&saved, path));
    rewrite_elim2_value(path, 2);
    assert(!game_load(&target, path));
    assert(target.life[0] == 777);
    unlink(path);
}

static void test_v1_state_migrates_with_zeroed_v2_trackers(void) {
    char path[] = "/tmp/mtg-game-v1-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    assert(fputs("P1=35\nP2=40\nP3=42\nP4=17\nFIRST=3\nMONARCH=2\n",
                 file) >= 0);
    assert(fclose(file) == 0);

    GameState game;
    assert(game_load(&game, path));
    assert(game.life[0] == 35);
    assert(game.life[2] == 42);
    assert(game.first_player == 2);
    assert(game.monarch_player == 1);
    for (int player = 0; player < GAME_PLAYER_COUNT; ++player) {
        assert(game.poison[player] == 0);
        for (int source = 0; source < GAME_PLAYER_COUNT; ++source) {
            assert(game.commander_damage[player][source] == 0);
        }
    }
    unlink(path);
}

static void test_incomplete_v2_state_is_rejected(void) {
    char path[] = "/tmp/mtg-game-v2-incomplete-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    assert(fputs("VERSION=2\nP1=40\nP2=40\nP3=40\nP4=40\n"
                 "FIRST=0\nMONARCH=0\n", file) >= 0);
    assert(fclose(file) == 0);

    GameState game;
    assert(!game_load(&game, path));
    unlink(path);
}

static void test_unversioned_state_rejects_v2_keys(void) {
    char path[] = "/tmp/mtg-game-unversioned-v2-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    assert(fputs("P1=40\nP2=40\nP3=40\nP4=40\n"
                 "CD11=1\nFIRST=0\nMONARCH=0\n", file) >= 0);
    assert(fclose(file) == 0);

    GameState game;
    game_reset(&game);
    game.life[0] = 123;
    assert(!game_load(&game, path));
    assert(game.life[0] == 123);
    unlink(path);
}

static void test_duplicate_state_key_is_rejected(void) {
    char path[] = "/tmp/mtg-game-bad-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    assert(fputs("P1=40\nP1=9\nP2=40\nP3=40\nP4=40\n", file) >= 0);
    assert(fclose(file) == 0);

    GameState game;
    assert(!game_load(&game, path));
    unlink(path);
}

int main(void) {
    test_reset_and_adjust();
    test_reset_clears_v2_trackers();
    test_reset_and_manual_player_state_hold_preserve_underlying_game();
    test_life_threshold_eliminates_and_manual_restore_waits_for_mutation();
    test_revive_removes_threshold_poison_and_restores_nonpositive_life();
    test_poison_and_single_commander_thresholds_eliminate();
    test_provisional_threshold_taps_do_not_poison_final_hold_state();
    test_zero_clear_holds_are_not_final_mutations();
    test_commander_damage_tap_tracks_source_and_life();
    test_commander_hold_refunds_only_that_sources_damage();
    test_commander_damage_hold_clears_and_refunds_life();
    test_commander_damage_actions_are_atomic_at_integer_limits();
    test_first_player_selection();
    test_adjustment_does_not_overflow();
    test_game_control_life_tap_and_hold_total_ten();
    test_game_control_commander_tap_and_hold_refund();
    test_game_control_poison_taps_and_hold_clears();
    test_poison_tap_stops_at_integer_limit();
    test_game_control_monarch_tap_assigns_and_active_hold_clears();
    test_monarch_hold_restores_press_time_owner();
    test_game_control_equality_uses_every_identity_field();

    test_life_preview_accumulates_nets_and_independent_deadlines();
    test_life_preview_expiry_clear_and_visual_clamp_preserve_true_net();

    test_state_round_trip();
    test_saved_state_declares_version_three();
    test_v2_state_migrates_with_players_active();
    test_v3_rejects_duplicate_and_non_boolean_elimination();
    test_v1_state_migrates_with_zeroed_v2_trackers();
    test_incomplete_v2_state_is_rejected();
    test_unversioned_state_rejects_v2_keys();
    test_duplicate_state_key_is_rejected();
    puts("PASS game core");
    return 0;
}
