/* pd2.c - PD2.EXE's own part: the set-up of its four tables
 * (setup_table0..3: the underwater table, presumably Neptune; Safari; the
 * one of REVENGE.FLI; Stall Turn) and make_off_colours, which PD2's
 * set-ups call where PD's call table_check.  See pd.h and pd1.c. */
#include "pd.h"

/* the lights' "off" colours (lights_off_pal) := the "on" colours halved,
 * C0h bytes (PD2's own) */
static void make_off_colours(void)
{
    uint16_t si = rw(V(lights_on_pal)), di = rw(V(lights_off_pal));
    int i;

    for (i = 0; i < 0xC0; i++)
        fwb(seg_tdata, (uint16_t)(di + i), (uint8_t)(frb(seg_tdata, (uint16_t)(si + i)) >> 1));
}

static void setup_table0(void)
{
    ww(V(count_target), 9);
    ww(V(table_palette), 0x1EB8);
    ww(V(lights_off_pal), 0x1F78);
    ww(V(lights_on_pal), 0x21B8);
    ww(V(light_count), 0x3D);
    grey_palettes();
    make_off_colours();
    ww(V(message), 0);
    ww(V(booster_col), 0xB);
    ww(V(map_events), 0x479F);
    ww(V(td_score_texts), 0x47B9);
    ww(V(td_idle_texts), 0x47CB);
    ww(V(td_idle_scroll), 0x47DB);
    ww(V(td_rects_lower), 0x4813);
    ww(V(td_rects_upper), 0x483B);
    ww(V(td_lanes_a_lower), 0x4845);
    ww(V(td_lanes_a_upper), 0x4859);
    ww(V(td_lanes_b_lower), 0x4863);
    ww(V(td_lanes_b_upper), 0x4877);
    ww(V(td_lights), 0x4881);
    ww(V(td_rotating), 0x48AB);
    ww(V(td_extra_ball), 0x3C03);
    ww(V(td_ign_timers), 0x48AF);
    ww(V(td_chains), 0x48B3);
    ww(V(td_random_lights), 0x48B9);
    ww(V(td_random_event), 0x48C3);
    ww(V(td_locks), 0x48C5);
    ww(V(td_objects), 0x48D1);
    ww(V(td_timed), 0x491F);
    ww(V(td_events), 0x4921);
    ww(V(td_hurry), 0x1697);
    ww(V(td_txt_hurry), 0x7336);
    ww(V(td_txt_jackpot2), 0x6B6C);
    ww(V(td_txt_jackpot3), 0x6B6C);
    ww(V(td_txt_player_ball), 0x6E53);
    ww(V(td_txt_jackpot), 0x6E68);
    ww(V(td_txt_bonus_x), 0x6E7D);
    ww(V(td_count_obj), 0x022D);
    ww(V(td_count_event), 0x0179);
    ww(V(td_count_obj2), 0x03A7);
    ww(V(td_count_texts), 0x0A19);
    ww(V(td_roulette), 0x0E53);
    ww(V(td_roulette_pick), 0x0E7D);
    ww(V(td_txt_relaunch), 0x7042);
    ww(V(td_txt_ball_lost), 0x7057);
    ww(V(td_txt_game_over), 0x706C);
    ww(V(td_txt_countdown), 0x7081);
    ww(V(td_txt_boosters), 0x7096);
    ww(V(lane_exit_x), 0x109);
    ww(V(ball_start_x_hi), 0x25);
    ww(V(ball_start_x), 0xC000);
    ww(V(ball_start_y_hi), 0x39);
    ww(V(ball_start_y), 0x4000);
    ww((uint16_t)(V(unused_obj1) + 0x4), 0xA);
    ww((uint16_t)(V(unused_obj2) + 0x4), 0x14);
    ww(V(sprite_frames), 0x7DF6);
    ww(V(flipper_mirror_x), 0x128);
    ww(V(flipper_x), 0x50);
    place_flipper_shapes(V(flipper_ys));
}

static void setup_table1(void)
{
    ww(V(count_target), 4);
    ww(V(table_palette), 0x2278);
    ww(V(lights_off_pal), 0x2338);
    ww(V(lights_on_pal), 0x2578);
    ww(V(light_count), 0x2E);
    grey_palettes();
    make_off_colours();
    ww(V(message), 0);
    ww(V(booster_col), 0x14);
    ww(V(map_events), 0x492D);
    ww(V(td_score_texts), 0x494F);
    ww(V(td_idle_texts), 0x47CB);
    ww(V(td_idle_scroll), 0x47DB);
    ww(V(td_rects_lower), 0x49BB);
    ww(V(td_rects_upper), 0x49CF);
    ww(V(td_lanes_a_lower), 0x4975);
    ww(V(td_lanes_a_upper), 0x4993);
    ww(V(td_lanes_b_lower), 0x499D);
    ww(V(td_lanes_b_upper), 0x49B1);
    ww(V(td_lights), 0x49D9);
    ww(V(td_rotating), 0x49F3);
    ww(V(td_extra_ball), 0x3F31);
    ww(V(td_ign_timers), 0x49F7);
    ww(V(td_chains), 0x49F9);
    ww(V(td_random_lights), 0x4A01);
    ww(V(td_random_event), 0x49FF);
    ww(V(td_locks), 0x4A09);
    ww(V(td_objects), 0x4A1B);
    ww(V(td_timed), 0x4A8D);
    ww(V(td_events), 0x4A93);
    ww(V(td_hurry), 0x1697);
    ww(V(td_txt_hurry), 0x7336);
    ww(V(td_txt_jackpot2), 0x7264);
    ww(V(td_txt_jackpot3), 0x7264);
    ww(V(td_txt_player_ball), 0x734B);
    ww(V(td_txt_jackpot), 0x7360);
    ww(V(td_txt_bonus_x), 0x7375);
    ww(V(td_txt_relaunch), 0x74C4);
    ww(V(td_txt_ball_lost), 0x74D9);
    ww(V(td_txt_game_over), 0x74EE);
    ww(V(td_txt_countdown), 0x7503);
    ww(V(td_txt_boosters), 0x7518);
    ww(V(td_count_obj), 0x0EFD);
    ww(V(td_count_event), 0x0E29);
    ww(V(td_count_obj2), 0x1065);
    ww(V(td_count_texts), 0x1867);
    ww(V(td_roulette), 0x0E53);
    ww(V(td_roulette_pick), 0x0E7D);
    ww(V(lane_exit_x), 0x113);
    ww(V(ball_start_x_hi), 0x25);
    ww(V(ball_start_x), 0xC000);
    ww(V(ball_start_y_hi), 0x39);
    ww(V(ball_start_y), 0x4000);
    ww((uint16_t)(V(unused_obj1) + 0x4), 0xA);
    ww((uint16_t)(V(unused_obj2) + 0x4), 0x14);
    ww(V(sprite_frames), 0x7DF6);
    ww(V(flipper_mirror_x), 0x127);
    ww(V(flipper_x), 0x50);
    place_flipper_shapes(V(flipper_ys));
}

static void setup_table2(void)
{
    ww(V(count_target), 1);
    ww(V(table_palette), 0x2638);
    ww(V(lights_off_pal), 0x26F8);
    ww(V(lights_on_pal), 0x2938);
    ww(V(light_count), 0x33);
    grey_palettes();
    make_off_colours();
    ww(V(message), 0);
    ww(V(booster_col), 0xA);
    ww(V(map_events), 0x4AA5);
    ww(V(td_score_texts), 0x4AC5);
    ww(V(td_idle_texts), 0x4AD5);
    ww(V(td_idle_scroll), 0x47DB);
    ww(V(td_rects_lower), 0x4B2B);
    ww(V(td_rects_upper), 0x4B49);
    ww(V(td_lanes_a_lower), 0x4AE5);
    ww(V(td_lanes_a_upper), 0x4B03);
    ww(V(td_lanes_b_lower), 0x4B0D);
    ww(V(td_lanes_b_upper), 0x4B21);
    ww(V(td_lights), 0x4B53);
    ww(V(td_rotating), 0x4B77);
    ww(V(td_extra_ball), 0x4261);
    ww(V(td_ign_timers), 0x48AF);
    ww(V(td_chains), 0x4B7D);
    ww(V(td_random_lights), 0x48B9);
    ww(V(td_random_event), 0x48C3);
    ww(V(td_locks), 0x4B89);
    ww(V(td_objects), 0x4B95);
    ww(V(td_timed), 0x4BE5);
    ww(V(td_events), 0x4BE9);
    ww(V(td_hurry), 0x2393);
    ww(V(td_txt_hurry), 0x7779);
    ww(V(td_txt_jackpot2), 0x76D1);
    ww(V(td_txt_jackpot3), 0x76D1);
    ww(V(td_txt_jackpot4), 0x76E6);
    ww(V(td_txt_player_ball), 0x77E2);
    ww(V(td_txt_jackpot), 0x77F7);
    ww(V(td_txt_bonus_x), 0x780C);
    ww(V(td_count_obj), 0x1D1B);
    ww(V(td_count_event), 0x1C91);
    ww(V(td_count_obj2), 0x1F15);
    ww(V(td_count_texts), 0x25C3);
    ww(V(td_roulette), 0x1C95);
    ww(V(td_roulette_pick), 0x1C9B);
    ww(V(td_txt_relaunch), 0x7946);
    ww(V(td_txt_ball_lost), 0x7970);
    ww(V(td_txt_game_over), 0x795B);
    ww(V(td_txt_countdown), 0x7985);
    ww(V(td_txt_boosters), 0x799A);
    ww(V(lane_exit_x), 0x118);
    ww(V(ball_start_x_hi), 0x25);
    ww(V(ball_start_x), 0xC000);
    ww(V(ball_start_y_hi), 0x39);
    ww(V(ball_start_y), 0x4000);
    ww((uint16_t)(V(unused_obj1) + 0x4), 0xA);
    ww((uint16_t)(V(unused_obj2) + 0x4), 0x14);
    ww(V(sprite_frames), 0x7DF6);
    ww(V(flipper_mirror_x), 0x127);
    ww(V(flipper_x), 0x50);
    place_flipper_shapes(V(flipper_ys));
}

static void setup_table3(void)
{
    ww(V(count_target), 1);
    ww(V(table_palette), 0x29F8);
    ww(V(lights_off_pal), 0x2AB8);
    ww(V(lights_on_pal), 0x2CF8);
    ww(V(light_count), 0x3E);
    grey_palettes();
    make_off_colours();
    ww(V(message), 0);
    ww(V(booster_col), 0xA);
    ww(V(map_events), 0x4BF7);
    ww(V(td_score_texts), 0x4C15);
    ww(V(td_idle_texts), 0x4C25);
    ww(V(td_idle_scroll), 0x4C35);
    ww(V(td_rects_lower), 0x4C77);
    ww(V(td_rects_upper), 0x4C95);
    ww(V(td_lanes_a_lower), 0x4C3B);
    ww(V(td_lanes_a_upper), 0x4C4F);
    ww(V(td_lanes_b_lower), 0x4C59);
    ww(V(td_lanes_b_upper), 0x4C6D);
    ww(V(td_lights), 0x4C9F);
    ww(V(td_rotating), 0x4CCF);
    ww(V(td_extra_ball), 0x46CB);
    ww(V(td_ign_timers), 0x4CD3);
    ww(V(td_chains), 0x4CD5);
    ww(V(td_random_lights), 0x48B9);
    ww(V(td_random_event), 0x48C3);
    ww(V(td_locks), 0x4CDF);
    ww(V(td_objects), 0x4CF1);
    ww(V(td_timed), 0x4D57);
    ww(V(td_events), 0x4D67);
    ww(V(td_hurry), 0x3353);
    ww(V(td_txt_hurry), 0x7BA7);
    ww(V(td_txt_jackpot2), 0x7B14);
    ww(V(td_txt_jackpot3), 0x7B14);
    ww(V(td_txt_jackpot4), 0x7B29);
    ww(V(td_txt_player_ball), 0x7BE6);
    ww(V(td_txt_jackpot), 0x7BFB);
    ww(V(td_txt_bonus_x), 0x7C10);
    ww(V(td_txt_relaunch), 0x7CF1);
    ww(V(td_txt_ball_lost), 0x7D1B);
    ww(V(td_txt_game_over), 0x7D06);
    ww(V(td_txt_countdown), 0x7D30);
    ww(V(td_txt_boosters), 0x7D45);
    ww(V(td_roulette), 0x2A31);
    ww(V(td_roulette_pick), 0x2A67);
    ww(V(lane_exit_x), 0x109);
    ww(V(ball_start_x_hi), 0x25);
    ww(V(ball_start_x), 0xC000);
    ww(V(ball_start_y_hi), 0x39);
    ww(V(ball_start_y), 0x4000);
    ww((uint16_t)(V(unused_obj1) + 0x4), 0xA);
    ww((uint16_t)(V(unused_obj2) + 0x4), 0x14);
    ww(V(sprite_frames), 0x7DF6);
    ww(V(flipper_mirror_x), 0x127);
    ww(V(flipper_x), 0x50);
    place_flipper_shapes(V(flipper_ys));
}

void pd2_setup_table(int table)
{
    switch (table) {
    case 0: setup_table0(); break;
    case 1: setup_table1(); break;
    case 2: setup_table2(); break;
    case 3: setup_table3(); break;
    }
}
