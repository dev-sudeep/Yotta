#include "git.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Run a git command and capture output ── */
static char *run_git(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    size_t cap = 4096, used = 0;
    char *buf = malloc(cap);
    if (!buf) { pclose(fp); return NULL; }
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        size_t n = strlen(line);
        if (used + n + 1 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); pclose(fp); return NULL; }
            buf = nb;
        }
        memcpy(buf + used, line, n);
        used += n;
    }
    buf[used] = '\0';
    pclose(fp);
    return buf;
}

void git_detect(const char *dir) {
    /* Check if inside a git repo */
    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd),
             "git -C \"%s\" rev-parse --is-inside-work-tree 2>/dev/null", dir);
    char *out = run_git(cmd);
    g.in_git_repo = (out && strncmp(out, "true", 4) == 0);
    free(out);

    if (!g.in_git_repo) {
        g.git_branch[0] = '\0';
        return;
    }

    /* Get current branch */
    snprintf(cmd, sizeof(cmd),
             "git -C \"%s\" rev-parse --abbrev-ref HEAD 2>/dev/null", dir);
    out = run_git(cmd);
    if (out) {
        size_t n = strlen(out);
        while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r')) n--;
        out[n] = '\0';
        strncpy(g.git_branch, out, sizeof(g.git_branch) - 1);
        free(out);
    }
}

/* Global status map (path -> status), populated by git_refresh_status */
#define STATUS_MAP_SIZE 2048
typedef struct { char path[MAX_PATH]; GitStatus status; } StatusEntry;
static StatusEntry status_map[STATUS_MAP_SIZE];
static int status_map_count = 0;

void git_refresh_status(void) {
    if (!g.in_git_repo) return;
    status_map_count = 0;

    char cmd[MAX_PATH + 128];
    snprintf(cmd, sizeof(cmd),
             "git -C \"%s\" status --porcelain -u 2>/dev/null", g.cwd);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    char line[MAX_PATH + 8];
    while (fgets(line, sizeof(line), fp) && status_map_count < STATUS_MAP_SIZE) {
        if (strlen(line) < 4) continue;
        char xy1 = line[0];
        char xy2 = line[1];
        /* path starts at index 3 */
        char path[MAX_PATH];
        strncpy(path, line + 3, sizeof(path) - 1);
        size_t n = strlen(path);
        while (n > 0 && (path[n-1] == '\n' || path[n-1] == '\r')) n--;
        path[n] = '\0';

        GitStatus st = GIT_STATUS_NONE;
        if (xy1 == '?' && xy2 == '?') st = GIT_STATUS_UNTRACKED;
        else if (xy1 == 'D' || xy2 == 'D') st = GIT_STATUS_DELETED;
        else if (xy1 != ' ' && xy1 != '?') st = GIT_STATUS_STAGED;
        else if (xy2 != ' ') st = GIT_STATUS_MODIFIED;

        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", g.cwd, path);

        strncpy(status_map[status_map_count].path, full, MAX_PATH - 1);
        status_map[status_map_count].status = st;
        status_map_count++;
    }
    pclose(fp);
}

GitStatus git_file_status(const char *filepath) {
    for (int i = 0; i < status_map_count; i++)
        if (strcmp(status_map[i].path, filepath) == 0)
            return status_map[i].status;
    return GIT_STATUS_NONE;
}

bool git_stage(const char *filepath) {
    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" add \"%s\" 2>/dev/null", g.cwd, filepath);
    return system(cmd) == 0;
}

bool git_unstage(const char *filepath) {
    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" restore --staged \"%s\" 2>/dev/null", g.cwd, filepath);
    return system(cmd) == 0;
}

bool git_commit(const char *message) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" commit -m \"%s\" 2>/dev/null", g.cwd, message);
    return system(cmd) == 0;
}

char *git_diff(const char *filepath) {
    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" diff \"%s\" 2>/dev/null", g.cwd, filepath);
    return run_git(cmd);
}
