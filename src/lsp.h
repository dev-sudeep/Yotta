#ifndef YOTTA_LSP_H
#define YOTTA_LSP_H

#include "types.h"

/* Start an LSP server for the given language server command.
   e.g. "clangd", "pylsp", "pyright-langserver --stdio" */
bool lsp_start(const char *server_cmd);

/* Shutdown and clean up LSP */
void lsp_shutdown(void);

/* Send textDocument/didOpen for the given file */
void lsp_did_open(const char *filepath, const char *lang, const char *content);

/* Send textDocument/didChange */
void lsp_did_change(const char *filepath, const char *content);

/* Send textDocument/completion request */
void lsp_request_completion(const char *filepath, int line, int character);

/* Read and process any pending LSP messages (non-blocking) */
void lsp_poll(void);

#endif /* YOTTA_LSP_H */
