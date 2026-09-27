/* pd1.c - PD.EXE's own part: the set-up of its four tables (Ignition,
 * Steel Wheel, Beat Box, Nightmare), setup_ignition .. setup_nightmare.
 * They fill the table descriptor (td_*) with the offsets of the table's
 * lists in DATA and set the table's constants; the lists themselves are
 * read from the program's memory.  See pd.h. */
#include "pd.h"

/* Ignition */
static void setup_ignition(void)
{
    ww(V(count_target), 9);
    ww(V(table_palette), 0x1EA8);
    ww(V(lights_off_pal), 0x1F68);
    ww(V(lights_on_pal), 0x201F);
    table_check();
    ww(V(light_count), 0x3D);
    grey_palettes();
    ww(V(message), 0);
    ww(V(booster_col), 0xB);
    ww(V(map_events), 0x46C4);
    ww(V(td_score_texts), 0x46DE);
    ww(V(td_idle_texts), 0x46F0);
    ww(V(td_idle_scroll), 0x4700);
    ww(V(td_rects_lower), 0x4738);
    ww(V(td_rects_upper), 0x4760);
    ww(V(td_lanes_a_lower), 0x476A);
    ww(V(td_lanes_a_upper), 0x477E);
    ww(V(td_lanes_b_lower), 0x4788);
    ww(V(td_lanes_b_upper), 0x479C);
    ww(V(td_lights), 0x47A6);
    ww(V(td_rotating), 0x47D0);
    ww(V(td_extra_ball), 0x3B28);
    ww(V(td_ign_timers), 0x47D4);
    ww(V(td_chains), 0x47D8);
    ww(V(td_random_lights), 0x47DE);
    ww(V(td_random_event), 0x47E8);
    ww(V(td_locks), 0x47EA);
    ww(V(td_objects), 0x47F6);
    ww(V(td_timed), 0x4844);
    ww(V(td_events), 0x4846);
    ww(V(td_hurry), 0x1694);
    ww(V(td_txt_hurry), 0x7247);
    ww(V(td_txt_jackpot2), 0x6A99);
    ww(V(td_txt_jackpot3), 0x6A99);
    ww(V(td_txt_player_ball), 0x6D80);
    ww(V(td_txt_jackpot), 0x6D95);
    ww(V(td_txt_bonus_x), 0x6DAA);
    ww(V(td_count_obj), 0x022C);
    ww(V(td_count_event), 0x0178);
    ww(V(td_count_obj2), 0x03A6);
    ww(V(td_count_texts), 0x0A18);
    ww(V(td_roulette), 0x0E50);
    ww(V(td_roulette_pick), 0x0E7A);
    ww(V(td_txt_relaunch), 0x6F53);
    ww(V(td_txt_ball_lost), 0x6F68);
    ww(V(td_txt_game_over), 0x6F7D);
    ww(V(td_txt_countdown), 0x6F92);
    ww(V(td_txt_boosters), 0x6FA7);
    ww(V(lane_exit_x), 0xF5);
    ww(V(ball_start_x_hi), 0x26);
    ww(V(ball_start_x), 0xC000);
    ww(V(ball_start_y_hi), 0x3B);
    ww(V(ball_start_y), 0);
    ww((uint16_t)(V(unused_obj1) + 0x4), 0xA);
    ww((uint16_t)(V(unused_obj2) + 0x4), 0x13);
    ww(V(sprite_frames), 0x7D82);
    ww(V(flipper_mirror_x), 0x121);
    ww(V(flipper_x), 0x4C);
    place_flipper_shapes(V(ign_flipper_ys));
}

/* Steel Wheel */
static void setup_steel_wheel(void)
{
    ww(V(count_target), 4);
    ww(V(table_palette), 0x20D6);
    ww(V(lights_off_pal), 0x2196);
    ww(V(lights_on_pal), 0x2220);
    table_check();
    ww(V(light_count), 0x2E);
    grey_palettes();
    ww(V(message), 0);
    ww(V(booster_col), 0x14);
    ww(V(map_events), 0x4852);
    ww(V(td_score_texts), 0x4874);
    ww(V(td_idle_texts), 0x46F0);
    ww(V(td_idle_scroll), 0x4700);
    ww(V(td_rects_lower), 0x48E0);
    ww(V(td_rects_upper), 0x48F4);
    ww(V(td_lanes_a_lower), 0x489A);
    ww(V(td_lanes_a_upper), 0x48B8);
    ww(V(td_lanes_b_lower), 0x48C2);
    ww(V(td_lanes_b_upper), 0x48D6);
    ww(V(td_lights), 0x48FE);
    ww(V(td_rotating), 0x4918);
    ww(V(td_extra_ball), 0x3E56);
    ww(V(td_ign_timers), 0x491C);
    ww(V(td_chains), 0x491E);
    ww(V(td_random_lights), 0x4926);
    ww(V(td_random_event), 0x4924);
    ww(V(td_locks), 0x492E);
    ww(V(td_objects), 0x4940);
    ww(V(td_timed), 0x49B2);
    ww(V(td_events), 0x49B8);
    ww(V(td_hurry), 0x1694);
    ww(V(td_txt_hurry), 0x7247);
    ww(V(td_txt_jackpot2), 0x7175);
    ww(V(td_txt_jackpot3), 0x7175);
    ww(V(td_txt_player_ball), 0x725C);
    ww(V(td_txt_jackpot), 0x7271);
    ww(V(td_txt_bonus_x), 0x7286);
    ww(V(td_txt_relaunch), 0x7436);
    ww(V(td_txt_ball_lost), 0x744B);
    ww(V(td_txt_game_over), 0x7460);
    ww(V(td_txt_countdown), 0x7475);
    ww(V(td_txt_boosters), 0x748A);
    ww(V(td_count_obj), 0x0EFA);
    ww(V(td_count_event), 0x0E26);
    ww(V(td_count_obj2), 0x1062);
    ww(V(td_count_texts), 0x1840);
    ww(V(td_roulette), 0x0E50);
    ww(V(td_roulette_pick), 0x0E7A);
    ww(V(lane_exit_x), 0x113);
    ww(V(ball_start_x_hi), 0x26);
    ww(V(ball_start_x), 0xC000);
    ww(V(ball_start_y_hi), 0x3B);
    ww(V(ball_start_y), 0);
    ww((uint16_t)(V(unused_obj1) + 0x4), 0xA);
    ww((uint16_t)(V(unused_obj2) + 0x4), 0x14);
    ww(V(sprite_frames), 0x7DE6);
    ww(V(flipper_mirror_x), 0x129);
    ww(V(flipper_x), 0x4F);
    place_flipper_shapes(V(flipper_ys));
}

/* Beat Box */
static void setup_beat_box(void)
{
    ww(V(count_target), 1);
    ww(V(table_palette), 0x22AA);
    ww(V(lights_off_pal), 0x236A);
    ww(V(lights_on_pal), 0x2403);
    table_check();
    ww(V(light_count), 0x33);
    grey_palettes();
    ww(V(message), 0);
    ww(V(booster_col), 0xA);
    ww(V(map_events), 0x49CA);
    ww(V(td_score_texts), 0x49EA);
    ww(V(td_idle_texts), 0x49FA);
    ww(V(td_idle_scroll), 0x4700);
    ww(V(td_rects_lower), 0x4A50);
    ww(V(td_rects_upper), 0x4A6E);
    ww(V(td_lanes_a_lower), 0x4A0A);
    ww(V(td_lanes_a_upper), 0x4A28);
    ww(V(td_lanes_b_lower), 0x4A32);
    ww(V(td_lanes_b_upper), 0x4A46);
    ww(V(td_lights), 0x4A78);
    ww(V(td_rotating), 0x4A9C);
    ww(V(td_extra_ball), 0x4186);
    ww(V(td_ign_timers), 0x47D4);
    ww(V(td_chains), 0x4AA2);
    ww(V(td_random_lights), 0x47DE);
    ww(V(td_random_event), 0x47E8);
    ww(V(td_locks), 0x4AAE);
    ww(V(td_objects), 0x4ABA);
    ww(V(td_timed), 0x4B0A);
    ww(V(td_events), 0x4B0E);
    ww(V(td_hurry), 0x236A);
    ww(V(td_txt_hurry), 0x76EB);
    ww(V(td_txt_jackpot2), 0x7643);
    ww(V(td_txt_jackpot3), 0x7643);
    ww(V(td_txt_jackpot4), 0x7658);
    ww(V(td_txt_player_ball), 0x7754);
    ww(V(td_txt_jackpot), 0x7769);
    ww(V(td_txt_bonus_x), 0x777E);
    ww(V(td_count_obj), 0x1CF2);
    ww(V(td_count_event), 0x1C68);
    ww(V(td_count_obj2), 0x1EEC);
    ww(V(td_count_texts), 0x259A);
    ww(V(td_roulette), 0x1C6C);
    ww(V(td_roulette_pick), 0x1C72);
    ww(V(td_txt_relaunch), 0x78BF);
    ww(V(td_txt_ball_lost), 0x78E9);
    ww(V(td_txt_game_over), 0x78D4);
    ww(V(td_txt_countdown), 0x78FE);
    ww(V(td_txt_boosters), 0x7913);
    ww(V(lane_exit_x), 0x118);
    ww(V(ball_start_x_hi), 0x26);
    ww(V(ball_start_x), 0xC000);
    ww(V(ball_start_y_hi), 0x3B);
    ww(V(ball_start_y), 0);
    ww((uint16_t)(V(unused_obj1) + 0x4), 0xA);
    ww((uint16_t)(V(unused_obj2) + 0x4), 0x14);
    ww(V(sprite_frames), 0x7DE6);
    ww(V(flipper_mirror_x), 0x128);
    ww(V(flipper_x), 0x4E);
    place_flipper_shapes(V(flipper_ys));
}

/* Nightmare */
static void setup_nightmare(void)
{
    ww(V(count_target), 1);
    ww(V(table_palette), 0x249C);
    ww(V(lights_off_pal), 0x255C);
    ww(V(lights_on_pal), 0x2616);
    table_check();
    ww(V(light_count), 0x3E);
    grey_palettes();
    ww(V(message), 0);
    ww(V(booster_col), 0xA);
    ww(V(map_events), 0x4B1C);
    ww(V(td_score_texts), 0x4B3A);
    ww(V(td_idle_texts), 0x4B4A);
    ww(V(td_idle_scroll), 0x4B5A);
    ww(V(td_rects_lower), 0x4BA6);
    ww(V(td_rects_upper), 0x4BC4);
    ww(V(td_lanes_a_lower), 0x4B60);
    ww(V(td_lanes_a_upper), 0x4B7E);
    ww(V(td_lanes_b_lower), 0x4B88);
    ww(V(td_lanes_b_upper), 0x4B9C);
    ww(V(td_lights), 0x4BCE);
    ww(V(td_rotating), 0x4BFE);
    ww(V(td_extra_ball), 0x45F0);
    ww(V(td_ign_timers), 0x4C02);
    ww(V(td_chains), 0x4C04);
    ww(V(td_random_lights), 0x47DE);
    ww(V(td_random_event), 0x47E8);
    ww(V(td_locks), 0x4C0E);
    ww(V(td_objects), 0x4C20);
    ww(V(td_timed), 0x4C84);
    ww(V(td_events), 0x4C94);
    ww(V(td_hurry), 0x3272);
    ww(V(td_txt_hurry), 0x7B35);
    ww(V(td_txt_jackpot2), 0x7A8D);
    ww(V(td_txt_jackpot3), 0x7A8D);
    ww(V(td_txt_jackpot4), 0x7AA2);
    ww(V(td_txt_player_ball), 0x7B74);
    ww(V(td_txt_jackpot), 0x7B89);
    ww(V(td_txt_bonus_x), 0x7B9E);
    ww(V(td_txt_relaunch), 0x7C7D);
    ww(V(td_txt_ball_lost), 0x7CA7);
    ww(V(td_txt_game_over), 0x7C92);
    ww(V(td_txt_countdown), 0x7CBC);
    ww(V(td_txt_boosters), 0x7CD1);
    ww(V(td_roulette), 0x2A08);
    ww(V(td_roulette_pick), 0x2A3E);
    ww(V(lane_exit_x), 0x109);
    ww(V(ball_start_x_hi), 0x26);
    ww(V(ball_start_x), 0xC000);
    ww(V(ball_start_y_hi), 0x3B);
    ww(V(ball_start_y), 0);
    ww((uint16_t)(V(unused_obj1) + 0x4), 0xA);
    ww((uint16_t)(V(unused_obj2) + 0x4), 0x14);
    ww(V(sprite_frames), 0x7DE6);
    ww(V(flipper_mirror_x), 0x129);
    ww(V(flipper_x), 0x4F);
    place_flipper_shapes(V(flipper_ys));
}

void pd1_setup_table(int table)
{
    switch (table) {
    case 0: setup_ignition(); break;
    case 1: setup_steel_wheel(); break;
    case 2: setup_beat_box(); break;
    case 3: setup_nightmare(); break;
    }
}
