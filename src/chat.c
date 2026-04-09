#include "chat.h"
#include "types.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>

/* ── Copilot CLI helpers ── */

/*
 * Run a command silently (stdin/stdout/stderr → /dev/null) and return its
 * exit code, or -1 on error.  Uses fork+exec to avoid any shell injection.
 */
static int run_silent(const char *prog, char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp(prog, argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/*
 * Check whether the copilot CLI is available.
 * We try (in order):
 *   1. gh copilot  (gh CLI with the copilot extension)
 *   2. copilot     (standalone copilot CLI)
 *
 * Returns the detected CLI identifier string, or NULL when not found.
 */
static const char *copilot_detect(void) {
    char *gh_args[] = { "gh", "copilot", "--version", NULL };
    if (run_silent("gh", gh_args) == 0) return "gh copilot";
    char *cp_args[] = { "copilot", "--version", NULL };
    if (run_silent("copilot", cp_args) == 0) return "copilot";
    return NULL;
}

static void copilot_check(void) {
    if (g.copilot_status != -1) return; /* already checked */
    const char *cmd = copilot_detect();
    g.copilot_status = cmd ? 1 : 0;
}

/*
 * Spawn the copilot CLI with `query` and connect its stdout to a pipe so the
 * main event loop can read the response asynchronously.
 * The query is passed as a direct exec argument to avoid shell injection.
 */
static void copilot_query(const char *query) {
    if (g.copilot_pid > 0) return; /* previous query still running */

    const char *cli = copilot_detect(); /* reuse detection, avoid duplication */
    if (!cli) return;

    int pipefd[2];
    if (pipe(pipefd) < 0) return;

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return; }

    if (pid == 0) {
        /* Child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        /* Redirect stdin from /dev/null so interactive prompts are skipped */
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }

        /* Exec directly (no shell) to avoid command injection */
        if (strcmp(cli, "gh copilot") == 0)
            execlp("gh", "gh", "copilot", "explain", query, (char *)NULL);
        else
            execlp("copilot", "copilot", "explain", query, (char *)NULL);
        _exit(127);
    }

    /* Parent */
    close(pipefd[1]);
    g.copilot_pid        = pid;
    g.copilot_stdout_fd  = pipefd[0];
    g.copilot_resp_len   = 0;
    g.copilot_resp_buf[0] = '\0';

    /* Non-blocking reads */
    int flags = fcntl(g.copilot_stdout_fd, F_GETFL, 0);
    fcntl(g.copilot_stdout_fd, F_SETFL, flags | O_NONBLOCK);
}

/*
 * Spawn the installer:  gh extension install github/gh-copilot
 * Uses direct exec (no shell) to avoid injection.
 */
static void copilot_install(void) {
    chat_add_message("Installing GitHub Copilot CLI extension…", false);
    g.copilot_install_prompt = false;

    int pipefd[2];
    if (pipe(pipefd) < 0) return;

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return; }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
        execlp("gh", "gh", "extension", "install", "github/gh-copilot",
               (char *)NULL);
        _exit(127);
    }

    /* Re-use the copilot pipe fields to stream install output */
    close(pipefd[1]);
    g.copilot_pid       = pid;
    g.copilot_stdout_fd = pipefd[0];
    g.copilot_resp_len  = 0;
    g.copilot_resp_buf[0] = '\0';
    int flags = fcntl(g.copilot_stdout_fd, F_GETFL, 0);
    fcntl(g.copilot_stdout_fd, F_SETFL, flags | O_NONBLOCK);

    /* After the process exits, re-check availability */
    g.copilot_status = -1;
}

/* ── Public API ── */

void chat_init(void) {
    g.chat_count = 0;
    g.chat_input_len = 0;
    g.chat_input[0] = '\0';
    g.chat_scroll = 0;
    g.copilot_status = -1; /* not yet checked */
    g.copilot_install_prompt = false;
    g.copilot_pid = 0;
    g.copilot_stdout_fd = -1;

    chat_add_message(
        "Hello! I'm your Copilot assistant. Ask me anything about your code.",
        false);
}

void chat_add_message(const char *text, bool is_user) {
    if (g.chat_count >= MAX_CHAT_MSGS) {
        free(g.chat_msgs[0].text);
        memmove(g.chat_msgs, g.chat_msgs + 1,
                (MAX_CHAT_MSGS - 1) * sizeof(ChatMessage));
        g.chat_count--;
    }
    ChatMessage *m = &g.chat_msgs[g.chat_count++];
    m->text    = strdup(text ? text : "");
    m->is_user = is_user;
    g.needs_redraw = true;
}

void chat_submit(void) {
    if (g.chat_input_len == 0) return;
    g.chat_input[g.chat_input_len] = '\0';

    /* ── Handle install confirmation ── */
    if (g.copilot_install_prompt) {
        char lower[8];
        int check_len = g.chat_input_len < 7 ? g.chat_input_len : 7;
        for (int i = 0; i < check_len; i++)
            lower[i] = (char)tolower((unsigned char)g.chat_input[i]);
        lower[check_len] = '\0';
        if (lower[0] == 'y') {
            copilot_install();
        } else {
            g.copilot_install_prompt = false;
            chat_add_message("Installation cancelled.", false);
        }
        g.chat_input_len = 0;
        g.chat_input[0] = '\0';
        g.needs_redraw = true;
        return;
    }

    /* Add user message */
    chat_add_message(g.chat_input, true);

    /* ── Check copilot availability on first use ── */
    copilot_check();

    if (g.copilot_status == 0) {
        /* Not installed — ask user */
        g.copilot_install_prompt = true;
        chat_add_message(
            "GitHub Copilot CLI not found.\n"
            "Type 'y' + Enter to install via: gh extension install github/gh-copilot\n"
            "Type 'n' + Enter to cancel.",
            false);
    } else if (g.copilot_pid > 0) {
        chat_add_message("Waiting for previous response…", false);
    } else {
        /* Spawn copilot query */
        chat_add_message("Asking Copilot…", false);
        copilot_query(g.chat_input);
    }

    g.chat_input_len = 0;
    g.chat_input[0] = '\0';
    g.needs_redraw = true;
}

/*
 * Called from the main event loop when copilot_stdout_fd is readable.
 * Reads all pending bytes; when the process exits appends the full response.
 */
void copilot_poll(void) {
    if (g.copilot_stdout_fd < 0) return;

    char tmp[4096];
    ssize_t n;
    while ((n = read(g.copilot_stdout_fd, tmp, sizeof(tmp) - 1)) > 0) {
        int avail = (int)sizeof(g.copilot_resp_buf) - g.copilot_resp_len - 1;
        if (avail > 0) {
            int copy = n < avail ? (int)n : avail;
            memcpy(g.copilot_resp_buf + g.copilot_resp_len, tmp, copy);
            g.copilot_resp_len += copy;
            g.copilot_resp_buf[g.copilot_resp_len] = '\0';
        }
        g.needs_redraw = true;
    }

    /* Check if child has finished */
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        int status;
        if (g.copilot_pid > 0)
            waitpid(g.copilot_pid, &status, WNOHANG);
        close(g.copilot_stdout_fd);
        g.copilot_pid = 0;
        g.copilot_stdout_fd = -1;

        /* Strip trailing whitespace and add response */
        int len = g.copilot_resp_len;
        while (len > 0 && (g.copilot_resp_buf[len - 1] == '\n' ||
                           g.copilot_resp_buf[len - 1] == '\r' ||
                           g.copilot_resp_buf[len - 1] == ' '))
            len--;
        g.copilot_resp_buf[len] = '\0';

        /* Remove any pending "Asking Copilot…" placeholder */
        if (g.chat_count > 0) {
            ChatMessage *last = &g.chat_msgs[g.chat_count - 1];
            if (!last->is_user &&
                strncmp(last->text, "Asking Copilot", 14) == 0) {
                free(last->text);
                g.chat_count--;
            }
        }

        if (g.copilot_resp_buf[0] != '\0') {
            chat_add_message(g.copilot_resp_buf, false);
        } else {
            /* Re-check: if we were installing, update availability */
            if (g.copilot_status == -1) {
                copilot_check();
                if (g.copilot_status == 1)
                    chat_add_message(
                        "GitHub Copilot CLI installed successfully! "
                        "Ask me anything.", false);
                else
                    chat_add_message(
                        "Installation may have failed. "
                        "Please check manually.", false);
            } else {
                chat_add_message("(No response from Copilot CLI.)", false);
            }
        }
        g.copilot_resp_len = 0;
        g.needs_redraw = true;
    }
}

void chat_handle_event(InputEvent *ev) {
    switch (ev->key) {
        case KEY_ENTER:
            chat_submit();
            break;
        case KEY_BACKSPACE:
            if (g.chat_input_len > 0)
                g.chat_input[--g.chat_input_len] = '\0';
            g.needs_redraw = true;
            break;
        case KEY_ARROW_UP:
            if (g.chat_scroll < g.chat_count - 1) {
                g.chat_scroll++;
                g.needs_redraw = true;
            }
            break;
        case KEY_ARROW_DOWN:
            if (g.chat_scroll > 0) {
                g.chat_scroll--;
                g.needs_redraw = true;
            }
            break;
        default:
            if (ev->codepoint >= 32 &&
                g.chat_input_len < (int)sizeof(g.chat_input) - 4) {
                uint32_t cp = ev->codepoint;
                char tmp[4]; int nb;
                if (cp < 0x80) { tmp[0]=(char)cp; nb=1; }
                else if (cp < 0x800) { tmp[0]=(char)(0xC0|(cp>>6)); tmp[1]=(char)(0x80|(cp&0x3F)); nb=2; }
                else { tmp[0]=(char)(0xE0|(cp>>12)); tmp[1]=(char)(0x80|((cp>>6)&0x3F)); tmp[2]=(char)(0x80|(cp&0x3F)); nb=3; }
                memcpy(g.chat_input + g.chat_input_len, tmp, nb);
                g.chat_input_len += nb;
                g.chat_input[g.chat_input_len] = '\0';
                g.needs_redraw = true;
            }
            break;
    }
}
