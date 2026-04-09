#include "git.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

/* ── Safe git executor using fork/execvp (no shell injection) ── */

/* Run git with argv-style args, capturing stdout. Returns malloc'd string (caller frees). */
static char *git_exec_capture(const char **argv) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return NULL;

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return NULL; }

    if (pid == 0) {
        /* Child */
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        /* Redirect stderr to /dev/null */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execvp("git", (char *const *)argv);
        _exit(127);
    }

    /* Parent */
    close(pipefd[1]);
    size_t cap = 4096, used = 0;
    char *buf = malloc(cap);
    if (!buf) { close(pipefd[0]); waitpid(pid, NULL, 0); return NULL; }

    char tmp[4096];
    ssize_t n;
    while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) {
        if (used + (size_t)n + 1 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); close(pipefd[0]); waitpid(pid, NULL, 0); return NULL; }
            buf = nb;
        }
        memcpy(buf + used, tmp, n);
        used += n;
    }
    buf[used] = '\0';
    close(pipefd[0]);
    int status;
    waitpid(pid, &status, 0);
    return buf;
}

/* Run git with argv-style args, discarding output. Returns true on success. */
static bool git_exec_run(const char **argv) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDOUT_FILENO); dup2(devnull, STDERR_FILENO); close(devnull); }
        execvp("git", (char *const *)argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}



void git_detect(const char *dir) {
    const char *argv[] = { "git", "-C", dir,
        "rev-parse", "--is-inside-work-tree", NULL };
    char *out = git_exec_capture(argv);
    g.in_git_repo = (out && strncmp(out, "true", 4) == 0);
    free(out);

    if (!g.in_git_repo) {
        g.git_branch[0] = '\0';
        return;
    }

    const char *argv2[] = { "git", "-C", dir,
        "rev-parse", "--abbrev-ref", "HEAD", NULL };
    out = git_exec_capture(argv2);
    if (out) {
        size_t n = strlen(out);
        while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r')) n--;
        out[n] = '\0';
        strncpy(g.git_branch, out, sizeof(g.git_branch) - 1);
        g.git_branch[sizeof(g.git_branch) - 1] = '\0';
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

    const char *argv[] = { "git", "-C", g.cwd,
        "status", "--porcelain", "-u", NULL };
    char *raw = git_exec_capture(argv);
    if (!raw) return;

    /* Parse line by line */
    char *saveptr = NULL;
    char *line = strtok_r(raw, "\n", &saveptr);
    while (line && status_map_count < STATUS_MAP_SIZE) {
        size_t llen = strlen(line);
        if (llen < 3) { line = strtok_r(NULL, "\n", &saveptr); continue; }
        /* Strip trailing \r */
        while (llen > 0 && line[llen-1] == '\r') line[--llen] = '\0';

        char xy1 = line[0];
        char xy2 = line[1];
        const char *rel_path = line + 3;

        GitStatus st = GIT_STATUS_NONE;
        if (xy1 == '?' && xy2 == '?') st = GIT_STATUS_UNTRACKED;
        else if (xy1 == 'D' || xy2 == 'D') st = GIT_STATUS_DELETED;
        else if (xy1 != ' ' && xy1 != '?') st = GIT_STATUS_STAGED;
        else if (xy2 != ' ') st = GIT_STATUS_MODIFIED;

        char full[MAX_PATH];
        int written = snprintf(full, sizeof(full), "%s/%s", g.cwd, rel_path);
        if (written > 0 && written < MAX_PATH) {
            strncpy(status_map[status_map_count].path, full, MAX_PATH - 1);
            status_map[status_map_count].path[MAX_PATH - 1] = '\0';
            status_map[status_map_count].status = st;
            status_map_count++;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(raw);
}

GitStatus git_file_status(const char *filepath) {
    for (int i = 0; i < status_map_count; i++)
        if (strcmp(status_map[i].path, filepath) == 0)
            return status_map[i].status;
    return GIT_STATUS_NONE;
}

bool git_stage(const char *filepath) {
    const char *argv[] = { "git", "-C", g.cwd, "add", filepath, NULL };
    return git_exec_run(argv);
}

bool git_unstage(const char *filepath) {
    const char *argv[] = { "git", "-C", g.cwd,
        "restore", "--staged", filepath, NULL };
    return git_exec_run(argv);
}

bool git_commit(const char *message) {
    const char *argv[] = { "git", "-C", g.cwd,
        "commit", "-m", message, NULL };
    return git_exec_run(argv);
}

char *git_diff(const char *filepath) {
    const char *argv[] = { "git", "-C", g.cwd, "diff", filepath, NULL };
    return git_exec_capture(argv);
}
