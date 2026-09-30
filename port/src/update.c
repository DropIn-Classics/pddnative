/* update.c - see update.h */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sys.h"
#include "update.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#endif
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
extern char **environ;
#endif

#define DAY (24 * 60 * 60)
#define MAX_JSON 65536

static char current[32];
static int started, have;
static UpdateInfo found;

/* ---- update.cfg: "consent = N", "checked = T" */

static int cfg_consent = -2;            /* -2: not read yet */
static long long cfg_checked;

static void data_path(char *out, size_t n, const char *name)
{
    char data[SYS_PATH];
    sys_data_dir(data, sizeof data);
    sys_join(out, n, data, name);
}

static void cfg_read(void)
{
    char path[SYS_PATH], line[128], name[32];
    long long v;
    FILE *f;

    if (cfg_consent != -2)
        return;
    cfg_consent = -1;
    data_path(path, sizeof path, "update.cfg");
    f = fopen(path, "r");
    while (f && fgets(line, sizeof line, f))
        if (sscanf(line, " %31[a-z] = %lld", name, &v) == 2) {
            if (!strcmp(name, "consent") && (v == 0 || v == 1))
                cfg_consent = (int)v;
            else if (!strcmp(name, "checked"))
                cfg_checked = v;
        }
    if (f)
        fclose(f);
}

static void cfg_write(void)
{
    char path[SYS_PATH];
    FILE *f;

    data_path(path, sizeof path, "update.cfg");
    f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "consent = %d\nchecked = %lld\n", cfg_consent, cfg_checked);
    fclose(f);
}

int update_consent(void)
{
    cfg_read();
    return cfg_consent;
}

void update_set_consent(int yes)
{
    cfg_read();
    cfg_consent = yes ? 1 : 0;
    cfg_write();
    if (!yes)
        have = 0;
}

/* ---- latest.json */

/* the string value of "key" in json, decoded (\uXXXX as UTF-8); 1 if there */
static int json_string(const char *json, const char *key, char *out, size_t n)
{
    char pat[40];
    const char *p = json;
    size_t k = 0;

    snprintf(pat, sizeof pat, "\"%s\"", key);
    while ((p = strstr(p, pat)) != NULL) {
        p += strlen(pat);
        while (isspace((unsigned char)*p))
            p++;
        if (*p != ':')
            continue;
        p++;
        while (isspace((unsigned char)*p))
            p++;
        if (*p != '"')
            continue;
        p++;
        while (*p && *p != '"') {
            char c = *p++;
            unsigned u;
            if (c == '\\' && *p) {
                c = *p++;
                switch (c) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u':
                    if (sscanf(p, "%4x", &u) != 1 || strlen(p) < 4)
                        return 0;
                    p += 4;
                    if (u >= 0xD800 && u < 0xE000)
                        u = '?';                /* a surrogate pair's half: not kept */
                    if (u < 0x80) {
                        c = (char)u;
                        break;
                    }
                    if (u < 0x800) {
                        if (k + 2 < n) {
                            out[k++] = (char)(0xC0 | u >> 6);
                            out[k++] = (char)(0x80 | (u & 0x3F));
                        }
                    } else if (k + 3 < n) {
                        out[k++] = (char)(0xE0 | u >> 12);
                        out[k++] = (char)(0x80 | (u >> 6 & 0x3F));
                        out[k++] = (char)(0x80 | (u & 0x3F));
                    }
                    continue;
                default: break;                 /* \" \\ \/ as they are */
                }
            }
            if (k + 1 < n)
                out[k++] = c;
        }
        if (n)
            out[k] = 0;
        return *p == '"';
    }
    return 0;
}

int update_parse(const char *json, UpdateInfo *info)
{
    memset(info, 0, sizeof *info);
    if (!json_string(json, "version", info->version, sizeof info->version) ||
        !(info->version[0] == 'v' || isdigit((unsigned char)info->version[0])))
        return 0;
    if (!json_string(json, "page", info->page, sizeof info->page) ||
        strncmp(info->page, "https://", 8) != 0)
        info->page[0] = 0;
    json_string(json, "notes", info->notes, sizeof info->notes);
    return 1;
}

int update_compare(const char *a, const char *b)
{
    if (*a == 'v' || *a == 'V')
        a++;
    if (*b == 'v' || *b == 'V')
        b++;
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y)
            return x < y ? -1 : 1;
        while (*a && !isdigit((unsigned char)*a))
            a++;
        while (*b && !isdigit((unsigned char)*b))
            b++;
    }
    return 0;
}

/* the kept latest.json read; `have` if it names a newer release */
static void read_kept(void)
{
    char path[SYS_PATH], *json;
    size_t size;
    uint8_t *buf;

    data_path(path, sizeof path, "latest.json");
    buf = sys_load(path, &size);
    if (!buf || size > MAX_JSON) {
        free(buf);
        return;
    }
    json = (char *)malloc(size + 1);
    if (json) {
        memcpy(json, buf, size);
        json[size] = 0;
        have = update_parse(json, &found) && update_compare(found.version, current) > 0;
        free(json);
    }
    free(buf);
}

/* ---- the fetch: into latest.part, in the background */

static int fetching;
static char part[SYS_PATH];

#ifdef _WIN32

static volatile LONG fetch_result;      /* 0 running, 1 done, 2 failed */
static wchar_t fetch_url[1024];

static DWORD WINAPI fetch_thread(LPVOID arg)
{
    URL_COMPONENTS uc;
    wchar_t host[256], path[1024];
    HINTERNET s = NULL, c = NULL, r = NULL;
    DWORD code = 0, len = sizeof code, got, total = 0;
    char buf[4096];
    FILE *f = NULL;
    int ok = 0;

    (void)arg;
    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host;
    uc.dwHostNameLength = sizeof host / sizeof host[0];
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = sizeof path / sizeof path[0];
    if (!WinHttpCrackUrl(fetch_url, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS)
        goto out;
    s = WinHttpOpen(L"doskit", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s)
        goto out;
    WinHttpSetTimeouts(s, 10000, 10000, 10000, 10000);
    c = WinHttpConnect(s, host, uc.nPort, 0);
    if (c)      /* GitHub's redirect to its file server is followed by WinHTTP */
        r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!r || !WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                                  0, 0, 0) || !WinHttpReceiveResponse(r, NULL))
        goto out;
    if (!WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX)
        || code != 200)
        goto out;
    f = fopen(part, "wb");
    if (!f)
        goto out;
    ok = 1;
    while (ok && WinHttpReadData(r, buf, sizeof buf, &got) && got > 0) {
        total += got;
        ok = total <= MAX_JSON && fwrite(buf, 1, got, f) == got;
    }
out:
    if (f && fclose(f) != 0)
        ok = 0;
    if (r)
        WinHttpCloseHandle(r);
    if (c)
        WinHttpCloseHandle(c);
    if (s)
        WinHttpCloseHandle(s);
    InterlockedExchange(&fetch_result, ok ? 1 : 2);
    return 0;
}

static int fetch_begin(const char *url)
{
    HANDLE t;

    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, fetch_url, sizeof fetch_url / sizeof fetch_url[0]))
        return 0;
    fetch_result = 0;
    t = CreateThread(NULL, 0, fetch_thread, NULL, 0, NULL);
    if (!t)
        return 0;
    CloseHandle(t);
    return 1;
}

/* -1 running, 0 failed, 1 done */
static int fetch_end(void)
{
    LONG r = InterlockedCompareExchange(&fetch_result, 0, 0);
    return r == 0 ? -1 : r == 1;
}

int update_open(const char *page)
{
    if (strncmp(page, "https://", 8) != 0)
        return 0;
    return (INT_PTR)ShellExecuteA(NULL, "open", page, NULL, NULL, SW_SHOWNORMAL) > 32;
}

#else

static pid_t fetch_pid, open_pid;

/* prog with args, its output and input /dev/null; 0 if it cannot be started */
static pid_t spawn(char *const argv[])
{
    posix_spawn_file_actions_t fa;
    pid_t pid;
    int r;

    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    r = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    return r == 0 ? pid : 0;
}

static int fetch_begin(const char *url)
{
    /* file:// for the kit's test; a redirect only to https */
    char *argv[] = { "curl", "-fsSL", "--max-time", "20", "--max-filesize", "65536",
                     "--proto", "=https,file", "--proto-redir", "=https",
                     "-o", part, (char *)url, NULL };
    fetch_pid = spawn(argv);
    return fetch_pid != 0;
}

static int fetch_end(void)
{
    int st;
    pid_t r = waitpid(fetch_pid, &st, WNOHANG);
    if (r == 0)
        return -1;
    return r == fetch_pid && WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

int update_open(const char *page)
{
#ifdef __APPLE__
    char *argv[] = { "open", (char *)page, NULL };
#else
    char *argv[] = { "xdg-open", (char *)page, NULL };
#endif
    if (strncmp(page, "https://", 8) != 0)
        return 0;
    if (open_pid)
        waitpid(open_pid, NULL, WNOHANG);
    open_pid = spawn(argv);
    return open_pid != 0;
}

#endif

void update_start(const char *version, const char *url)
{
    long long now = (long long)time(NULL);
    char kept[SYS_PATH];

    if (started || !version || !*version || !url || !*url || update_consent() != 1)
        return;
    started = 1;
    snprintf(current, sizeof current, "%s", version);
    data_path(kept, sizeof kept, "latest.json");
    if (sys_is_file(kept) && now >= cfg_checked && now - cfg_checked < DAY) {
        read_kept();
        return;
    }
    data_path(part, sizeof part, "latest.part");
    remove(part);
    fetching = fetch_begin(url);
}

int update_poll(UpdateInfo *info)
{
    if (fetching) {
        int r = fetch_end();
        if (r >= 0) {
            fetching = 0;
            if (r) {
                char kept[SYS_PATH];
                data_path(kept, sizeof kept, "latest.json");
                remove(kept);                   /* Windows renames only onto nothing */
                if (sys_rename(part, kept) == 0) {
                    cfg_checked = (long long)time(NULL);
                    cfg_write();
                    read_kept();
                }
            } else {
                remove(part);
            }
        }
    }
    if (have && update_consent() == 1) {
        *info = found;
        return 1;
    }
    return 0;
}
