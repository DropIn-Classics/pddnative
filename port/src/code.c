/* code.c - routines whose offsets the program keeps in its data: the
 * state table, frame_callback, the event objects' handlers.  call_code()
 * finds the C function for an offset in CODE by the names of the hints.
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
    FN(st_load), FN(st_idle), FN(st_restart), FN(st_quit), FN(st_exit),
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
