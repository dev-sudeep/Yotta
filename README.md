# Yotta

A high-performance, modern terminal-based code editor inspired by Visual Studio Code and LunarVim — built entirely in C with no external UI libraries.

## Features

- **Four-pane layout** — File Explorer · Code Editor · Copilot Chat · Integrated Terminal
- **Syntax highlighting** for C/C++, Python, JSON, Bash
- **Multiple tabs** with easy switching
- **Vim-like normal/insert mode** editing
- **PTY terminal** — fully interactive shell (bash/zsh/dash) in the bottom pane
- **Git integration** — file status indicators (M/S/?/D), branch display, diff, stage, commit
- **LSP client** — connect any Language Server via pipes (clangd, pylsp, etc.)
- **Copilot-style chat pane** — pluggable AI assistant (connect your own backend)
- **Mouse & touch support** — click to focus panes, click tabs to switch, scroll
- **Double-buffered rendering** — differential screen updates at ≈60 FPS
- **OSC 11** soft dark background hint
- **Zero external dependencies** — only standard C11 + POSIX

## Building

```bash
make
```

Requires GCC (or Clang) and standard POSIX headers (`pty.h` / `util.h`).

```bash
# Install to /usr/local/bin
make install
```

## Usage

```bash
./yotta [file ...]
```

### Keyboard shortcuts

| Shortcut        | Action                    |
|-----------------|---------------------------|
| `Ctrl+Q`        | Quit                      |
| `Ctrl+S`        | Save current file         |
| `Ctrl+B`        | Toggle file explorer      |
| `Ctrl+T`        | Toggle terminal           |
| `Ctrl+L`        | Focus editor pane         |
| `Ctrl+P`        | Open file search          |
| `Ctrl+Tab`      | Next tab                  |
| `Ctrl+W`        | Close current tab         |
| `Ctrl+Shift+C`  | Toggle Copilot chat       |
| `Ctrl+N`        | Cycle pane focus          |

### Editor (Vim-like normal mode)

| Key        | Action                   |
|------------|--------------------------|
| `i/a/o/O`  | Enter insert mode        |
| `h/j/k/l`  | Move cursor              |
| `w/b`      | Word forward/backward    |
| `0/$`      | Line start/end           |
| `g/G`      | File start/end           |
| `x/d`      | Delete char/line         |
| `/`        | Search                   |
| `:`        | Command mode             |
| `Esc`      | Return to normal mode    |

## Project Structure

```
src/
  main.c        Entry point, event loop, pane routing
  ui.c/h        Double-buffered ANSI rendering engine
  input.c/h     Raw mode, keyboard + mouse parsing
  editor.c/h    Text buffer, cursors, tabs, editing
  highlight.c/h Syntax highlighting (C, Python, JSON, Bash)
  explorer.c/h  File tree, directory navigation
  git.c/h       Git status, stage, commit, diff
  terminal.c/h  PTY shell (fork + forkpty)
  lsp.c/h       LSP client (JSON-RPC over pipes)
  chat.c/h      Copilot-style chat pane
  types.h       Shared types, constants, global state
Makefile
```

## Version

0.1.0
