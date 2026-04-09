#include "lsp.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

/* ── JSON-RPC helpers ── */

static void lsp_send(const char *json) {
    if (!g.lsp.initialized && g.lsp.pid <= 0) return;
    int jlen = (int)strlen(json);
    char header[64];
    int hlen = snprintf(header, sizeof(header),
                        "Content-Length: %d\r\n\r\n", jlen);
    if (write(g.lsp.stdin_fd, header, hlen) < 0) return;
    (void)write(g.lsp.stdin_fd, json, jlen);
}

static int next_id(void) {
    return ++g.lsp.req_id;
}

/* ── Start LSP ── */

bool lsp_start(const char *server_cmd) {
    if (g.lsp.pid > 0) return true; /* already running */

    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) return false;

    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        /* Child */
        dup2(stdin_pipe[0],  STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        /* Redirect stderr to /dev/null */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execl("/bin/sh", "/bin/sh", "-c", server_cmd, NULL);
        _exit(127);
    }

    /* Parent */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    g.lsp.pid       = pid;
    g.lsp.stdin_fd  = stdin_pipe[1];
    g.lsp.stdout_fd = stdout_pipe[0];
    g.lsp.req_id    = 0;

    /* Set stdout non-blocking */
    int flags = fcntl(g.lsp.stdout_fd, F_GETFL, 0);
    fcntl(g.lsp.stdout_fd, F_SETFL, flags | O_NONBLOCK);

    /* Send initialize request */
    char json[1024];
    snprintf(json, sizeof(json),
        "{\"jsonrpc\":\"2.0\",\"id\":%d,"
        "\"method\":\"initialize\","
        "\"params\":{\"processId\":%d,"
        "\"rootUri\":\"file://%s\","
        "\"capabilities\":{"
        "\"textDocument\":{"
        "\"completion\":{\"dynamicRegistration\":false},"
        "\"hover\":{\"dynamicRegistration\":false}"
        "}}}}",
        next_id(), (int)getpid(), g.cwd);
    lsp_send(json);
    g.lsp.initialized = true;
    return true;
}

void lsp_shutdown(void) {
    if (g.lsp.pid <= 0) return;
    const char *shutdown_json =
        "{\"jsonrpc\":\"2.0\",\"id\":9999,\"method\":\"shutdown\",\"params\":null}";
    lsp_send(shutdown_json);
    const char *exit_json =
        "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    lsp_send(exit_json);
    close(g.lsp.stdin_fd);
    close(g.lsp.stdout_fd);
    int status;
    waitpid(g.lsp.pid, &status, WNOHANG);
    g.lsp.pid = 0;
    g.lsp.initialized = false;
}

void lsp_did_open(const char *filepath, const char *lang, const char *content) {
    if (!g.lsp.initialized) return;
    /* Build URI */
    char uri[MAX_PATH + 8];
    snprintf(uri, sizeof(uri), "file://%s", filepath);

    /* Escape content (simple: replace \ with \\ and " with \") */
    int clen = (int)strlen(content);
    char *escaped = malloc(clen * 2 + 1);
    if (!escaped) return;
    int ei = 0;
    for (int i = 0; i < clen; i++) {
        if (content[i] == '\\') { escaped[ei++] = '\\'; escaped[ei++] = '\\'; }
        else if (content[i] == '"') { escaped[ei++] = '\\'; escaped[ei++] = '"'; }
        else if (content[i] == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; }
        else if (content[i] == '\r') { escaped[ei++] = '\\'; escaped[ei++] = 'r'; }
        else escaped[ei++] = content[i];
    }
    escaped[ei] = '\0';

    /* Allocate enough for the JSON */
    int jcap = ei + 512;
    char *json = malloc(jcap);
    if (!json) { free(escaped); return; }
    snprintf(json, jcap,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\","
        "\"params\":{\"textDocument\":{"
        "\"uri\":\"%s\","
        "\"languageId\":\"%s\","
        "\"version\":1,"
        "\"text\":\"%s\"}}}",
        uri, lang, escaped);
    lsp_send(json);
    free(json);
    free(escaped);
}

void lsp_did_change(const char *filepath, const char *content) {
    if (!g.lsp.initialized) return;
    char uri[MAX_PATH + 8];
    snprintf(uri, sizeof(uri), "file://%s", filepath);

    int clen = (int)strlen(content);
    char *escaped = malloc(clen * 2 + 1);
    if (!escaped) return;
    int ei = 0;
    for (int i = 0; i < clen; i++) {
        if (content[i] == '\\') { escaped[ei++] = '\\'; escaped[ei++] = '\\'; }
        else if (content[i] == '"') { escaped[ei++] = '\\'; escaped[ei++] = '"'; }
        else if (content[i] == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; }
        else if (content[i] == '\r') { escaped[ei++] = '\\'; escaped[ei++] = 'r'; }
        else escaped[ei++] = content[i];
    }
    escaped[ei] = '\0';

    int jcap = ei + 512;
    char *json = malloc(jcap);
    if (!json) { free(escaped); return; }
    snprintf(json, jcap,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\","
        "\"params\":{\"textDocument\":{\"uri\":\"%s\",\"version\":2},"
        "\"contentChanges\":[{\"text\":\"%s\"}]}}",
        uri, escaped);
    lsp_send(json);
    free(json);
    free(escaped);
}

void lsp_request_completion(const char *filepath, int line, int character) {
    if (!g.lsp.initialized) return;
    char uri[MAX_PATH + 8];
    snprintf(uri, sizeof(uri), "file://%s", filepath);
    char json[512];
    snprintf(json, sizeof(json),
        "{\"jsonrpc\":\"2.0\",\"id\":%d,"
        "\"method\":\"textDocument/completion\","
        "\"params\":{\"textDocument\":{\"uri\":\"%s\"},"
        "\"position\":{\"line\":%d,\"character\":%d}}}",
        next_id(), uri, line, character);
    lsp_send(json);
}

void lsp_poll(void) {
    if (g.lsp.pid <= 0) return;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(g.lsp.stdout_fd, &fds);
    struct timeval tv = {0, 0};
    if (select(g.lsp.stdout_fd + 1, &fds, NULL, NULL, &tv) <= 0) return;

    /* Read header */
    char buf[65536];
    ssize_t n = read(g.lsp.stdout_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';
    /* Parse Content-Length and JSON — for now just discard */
    /* TODO: parse diagnostics, completions, etc. */
}
