/* update.h - newer releases of the port made known, on the player's say
 * (docs/RELEASE.md, point 7).
 *
 * A release carries latest.json (the template's workflow makes it), which
 * a release build fetches from PORT_UPDATE_URL at most once a day, in the
 * background, when the player said yes: WinHTTP on Windows, the system's
 * curl elsewhere.  The file is kept in sys_data_dir as latest.json and
 * read from there until the next fetch; the answer and the time of the
 * last fetch are in update.cfg beside it.  A failed fetch says nothing.
 *
 * On the setup screen, each frame:
 *
 *     if (update_consent() < 0)        ask once, then update_set_consent
 *     update_start(PORT_VERSION, PORT_UPDATE_URL);    (does its work once,
 *                                      when the player has said yes)
 *     if (update_poll(&u))             show u.version, u.notes;
 *                                      a key: update_open(u.page)
 */
#ifndef PD_UPDATE_H
#define PD_UPDATE_H

typedef struct {
    char version[32];   /* "v1.3" */
    char page[256];     /* the release's page, https:// */
    char notes[512];    /* for players, lines split by '\n'; may be empty */
} UpdateInfo;

/* the player's answer: -1 not asked yet, 0 no, 1 yes */
int update_consent(void);
void update_set_consent(int yes);

/* Looks for a newer release than `version` if the player said yes and
 * both are given (NULL or "": a build without them, nothing done): the
 * kept latest.json if fetched less than a day ago, else a fetch started.
 * Cheap to call each frame: it acts once, the first time after a yes. */
void update_start(const char *version, const char *url);

/* 1 once a release newer than update_start's version is known, *info
 * filled in; 0 until then or if there is none */
int update_poll(UpdateInfo *info);

/* the release's page in the browser; only https:// addresses.  1 if the
 * browser was asked to. */
int update_open(const char *page);

/* for tests: latest.json's fields (1 if it has a version); a before b
 * (<0), the same (0), after (>0), "v1.10" after "v1.9" */
int update_parse(const char *json, UpdateInfo *info);
int update_compare(const char *a, const char *b);

#endif
