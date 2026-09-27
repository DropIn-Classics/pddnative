/* code.c - routines whose offsets the program keeps in its data: the
 * state table, frame_callback, the event objects' handlers.  call_code()
 * and call_handler() find the C function for an offset in CODE by the
 * names of the hints.
 * See pd.h. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "pd.h"

typedef struct {
    size_t field;               /* the name's place in PdNames */
    void (*fn)(void);
} CodeFn;

#define FN(name) {offsetof(PdNames, name), name}
static const CodeFn ported[] = {
    FN(st_load), FN(st_idle), FN(st_ball_start), FN(st_play), FN(st_ball_lost),
    FN(st_game_over), FN(st_ball_locked), FN(st_pause), FN(st_tilt), FN(st_12),
    FN(st_restart), FN(st_quit), FN(st_exit),
    FN(no_callback), FN(upload_lights),
};

/* every name, to say which routine is missing */
typedef struct {
    const char *seg, *name;
    size_t field;
} NameRef;
#define PDN_REF(seg, name, a1, a2) {#seg, #name, offsetof(PdNames, name)},
static const NameRef names[] = {PDN_NAMES(PDN_REF)};

static uint16_t value(size_t field)
{
    uint16_t v;
    memcpy(&v, (const char *)&nm + field, sizeof v);
    return v;
}

/* the event objects' handlers (+16h): DI = the object; 1 for ZF (a
 * running object's handler: it is done) */
typedef struct {
    size_t field;
    int (*fn)(uint16_t di);
} HandlerFn;

#define HN(name) {offsetof(PdNames, name), name}
static const HandlerFn handlers[] = {
    HN(obj_nop1), HN(obj_nop2), HN(obj_nop3), HN(obj_nop4), HN(obj_nop5),
    HN(obj_nop6), HN(mult_2), HN(mult_3), HN(mult_4), HN(mult_5), HN(mult_6),
    HN(mult_7), HN(mult_8), HN(mult_10), HN(advance_object),
    HN(extra_ball_award), HN(add_hurry_value), HN(double_score),
    HN(double_bonus), HN(hold_bonus), HN(count_message), HN(collect_jackpot),
    HN(raise_jackpot), HN(lock_jackpot), HN(score_to_best),
    HN(nightmare_switch), HN(countdown), HN(roulette), HN(light_locks),
    HN(lock_ball), HN(lock_ball2), HN(lock_ball3),
};

int call_handler(uint16_t offset, uint16_t di)
{
    size_t i;

    for (i = 0; i < sizeof handlers / sizeof handlers[0]; i++)
        if (value(handlers[i].field) == offset)
            return handlers[i].fn(di);
    call_code(offset);                  /* not ported: ends with its name */
    return 1;
}

void call_code(uint16_t offset)
{
    size_t i;
    char what[64];

    for (i = 0; i < sizeof ported / sizeof ported[0]; i++)
        if (value(ported[i].field) == offset) {
            ported[i].fn();
            return;
        }
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(names[i].seg, "CODE") && value(names[i].field) == offset)
            not_ported(names[i].name);
    snprintf(what, sizeof what, "the routine at CODE:%04X", offset);
    not_ported(what);
}
