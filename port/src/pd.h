/* pd.h - the table programs' engine (PD.EXE, PD2.EXE) in C.
 *
 * The routines are those of the hints (src/PD.hints), under the same
 * names, translated from the generated source (build/PD.ASM); the
 * comments at each say what the hints' comments say, shorter.  They work
 * on the program's memory (mem.h), so the tables' records are used where
 * they lie and one engine runs both programs.
 *
 * Conventions:
 *   - a routine that returned a result in the carry flag returns it as an
 *     int (1 = CF set, which is failure or "yes" as the comment says);
 *   - registers that carried arguments are parameters, named after what
 *     they hold;
 *   - a routine that is not translated yet calls not_ported(), which ends
 *     the program with its name (the port is being built routine by
 *     routine in the order the game runs them).
 */
#ifndef PD_PD_H
#define PD_PD_H

#include <stddef.h>
#include <stdint.h>
#include "mem.h"

/* ---- the program's end: quit, a fatal error (pd_main.c) */

/* the program ends (st_exit, the window closed): back to pd_run's caller */
void pd_exit(void);
/* fatal_error: a file could not be loaded; the text is shown, the program ends */
void pd_fatal(const char *what);
/* a routine not translated yet: ends the program with its name */
void not_ported(const char *name);
/* PD_STOP=where#N ends the program the Nth time it gets here */
void checkpoint(const char *where);
/* one picture (frame_wait); the program ends when the window was closed */
void pump_frame(void);

/* runs the program loaded (mem_load) with the table `table` (0-3, the
 * command line's digit).  Returns when it ends; 0, or -1 with a message
 * in err after a fatal error. */
int pd_run(int table, const char *game_dir, char *err, size_t n);

extern const char *pd_game_dir;

/* ---- code addresses: routines stored in the program's data (the state
 * table, frame_callback, the event objects' handlers) */
void call_code(uint16_t offset);
/* an event object's handler (its +16h) for the object at di; 1 when it
 * returned ZF (a running object's handler is done) */
int call_handler(uint16_t offset, uint16_t di);

/* ---- pd_main.c: start, the states */
void st_load(void);
void st_idle(void);
void st_ball_start(void);
void st_ball_locked(void);
void st_play(void);
void st_ball_lost(void);
void st_game_over(void);
void st_pause(void);
void st_tilt(void);
void st_quit(void);
void st_exit(void);
void st_restart(void);
void st_12(void);
void load_table_files(void);
void table_setup(void);
void place_flipper_shapes(uint16_t ys);
void init_lights(void);
void format_hiscores(void);
void index_flipper_shapes(void);
void grey_palettes(void);
void table_check(void);
void no_callback(void);

/* ---- pd_idle.c: state 2 */
void game_scroll_down(void);

/* ---- pd_game.c: a new game, a new ball */
void new_game(void);
void reset_ball(void);
void clear_object_timers(void);

/* ---- pd_events.c: DI = a sequence, its objects pushed on the event stack */
void run_event(uint16_t di);

/* ---- pd_rules.c: state 5, a frame of play */
void st_play(void);
/* the 6-byte BCD number at bp added to the one at di; idle_timer restarts */
void bcd_add(uint16_t di, uint16_t bp);

/* ---- pd_play.c: state 3, the nudge, the plunger, the scroll */
void ball_start(void);
void scroll_follow(void);
void nudge(void);
void plunger(void);

/* ---- pd_ball.c: the ball and the flippers */
void ball_frame(void);
void ball_step(void);
void draw_flippers(void);
/* the row index at BSS:0 for the map the far pointer at `map` points to */
void index_map(uint16_t map);

/* ---- pd_lights.c */
void light_on(uint16_t light);
void light_off(uint16_t light);
void lights_off_all(void);
void lights_frame(void);
void idle_lights(void);

/* ---- pd1.c / pd2.c: each program's own set-up of its four tables */
void pd1_setup_table(int table);
void pd2_setup_table(int table);

/* ---- pd_video.c: the screen */
void setup_screen(void);
void set_mode_x(void);
void set_mode_350(void);
void black_palette(int count);
void set_split(void);
void clear_vram(void);
void set_planes(uint8_t map_mask, uint8_t read_map);
void wait_frames(int n);
void wait_frame(void);
void write_mode_0(void);
void write_mode_1(void);
void screen_off(void);
void screen_on(void);
void set_palette(uint16_t seg, uint16_t si);
void copy_to_vram(uint16_t di, uint16_t seg, uint16_t count);
void scroll_step(void);
void scroll_redraw(void);   /* L14BB: the start address from scroll */
void set_screen_start(void);
void upload_lights(void);

/* ---- pd_sprite.c */
void draw_sprites(void);
void set_sprite_frame(uint16_t frame, uint16_t sprite);
void make_sprite_shapes(uint16_t seg, uint16_t di, uint16_t bp);

/* ---- pd_keys.c: the keyboard (INT 9) */
void kbd_int(uint8_t scancode);
void wait_keys_up(void);
/* key_map's bit for a scan code (E0 keys + 80h) */
int key_down(int code);

/* ---- pd_files.c: files (INT 21h) */
/* the host path of the DOS file name at seg:off (see pd_files.c); 0 when
 * the file is there */
int dos_path(uint16_t seg, uint16_t off, char *out, size_t n);
int load_file(uint16_t name, uint16_t seg, uint16_t di);      /* 1 = failed */
int write_file(uint16_t name, uint16_t seg, uint16_t di);     /* [file_size] bytes */
void load_hiscores(void);
void save_hiscores(void);
void load_options(void);

/* ---- pd_sound.c: the INT 66h driver's side */
int load_sound_driver(void);        /* 1 = failed */
int start_music(void);              /* 1 = failed */
void stop_sound(void);
void timer_callback(void);

/* ---- pd_text.c: the display */
void show_text(uint16_t text);
void draw_char(uint8_t c);
void end_running(void);
/* a frame of the display message at msg: 1 while it runs (message_start
 * also begins it; message_step ends the running object with it) */
int message_start(uint16_t msg);
int message_step(uint16_t msg);

#endif
