#ifndef YOTTA_TYPES_H
#define YOTTA_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

/* ── Version ── */
#define YOTTA_VERSION "0.1.0"

/* ── Terminal limits ── */
#define MAX_ROWS      512
#define MAX_COLS      512
#define MAX_PATH      4096
#define MAX_LINES     1000000
#define MAX_LINE_LEN  4096
#define MAX_TABS      32
#define MAX_CHAT_MSGS 512
#define MAX_EXPLORER_NODES 4096

/* ── Color palette (256-color + true-color) ── */
/* Background */
#define COL_BG          "\x1b[48;2;18;18;28m"
#define COL_BG_PANEL    "\x1b[48;2;22;22;34m"
#define COL_BG_ACTIVE   "\x1b[48;2;28;28;44m"
#define COL_BG_SEL      "\x1b[48;2;42;42;72m"
#define COL_BG_CURSOR   "\x1b[48;2;80;80;140m"
#define COL_BG_STATUS   "\x1b[48;2;30;30;50m"
#define COL_BG_TAB      "\x1b[48;2;24;24;38m"
#define COL_BG_TAB_ACT  "\x1b[48;2;40;40;64m"

/* Foreground */
#define COL_FG          "\x1b[38;2;220;220;240m"
#define COL_FG_DIM      "\x1b[38;2;100;100;140m"
#define COL_FG_BRIGHT   "\x1b[38;2;255;255;255m"
#define COL_FG_ACCENT   "\x1b[38;2;130;170;255m"
#define COL_FG_GREEN    "\x1b[38;2;100;220;100m"
#define COL_FG_RED      "\x1b[38;2;240;100;100m"
#define COL_FG_YELLOW   "\x1b[38;2;240;200;80m"
#define COL_FG_CYAN     "\x1b[38;2;80;220;220m"
#define COL_FG_MAGENTA  "\x1b[38;2;200;100;240m"
#define COL_FG_ORANGE   "\x1b[38;2;255;165;80m"
#define COL_FG_BORDER   "\x1b[38;2;60;60;100m"
#define COL_FG_LNUM     "\x1b[38;2;80;80;120m"
#define COL_FG_MODIFIED "\x1b[38;2;255;160;60m"
#define COL_FG_UNTRACK  "\x1b[38;2;200;100;100m"
#define COL_FG_STAGED   "\x1b[38;2;80;200;120m"
#define COL_RESET       "\x1b[0m"

/* Bold / Italic */
#define BOLD   "\x1b[1m"
#define ITALIC "\x1b[3m"
#define DIM    "\x1b[2m"

/* ── Pane IDs ── */
typedef enum {
    PANE_EXPLORER = 0,
    PANE_EDITOR,
    PANE_CHAT,
    PANE_TERMINAL,
    PANE_COUNT
} PaneId;

/* ── Editor mode ── */
typedef enum {
    MODE_NORMAL = 0,
    MODE_INSERT,
    MODE_VISUAL,
    MODE_COMMAND,
    MODE_SEARCH
} EditorMode;

/* ── Key codes ── */
typedef enum {
    KEY_NONE = 0,
    KEY_CTRL_A = 1, KEY_CTRL_B, KEY_CTRL_C, KEY_CTRL_D,
    KEY_CTRL_E, KEY_CTRL_F, KEY_CTRL_G, KEY_CTRL_H,
    KEY_TAB = 9,
    KEY_CTRL_J = 10, KEY_CTRL_K, KEY_CTRL_L, KEY_ENTER = 13,
    KEY_CTRL_N = 14, KEY_CTRL_O, KEY_CTRL_P, KEY_CTRL_Q,
    KEY_CTRL_R, KEY_CTRL_S, KEY_CTRL_T, KEY_CTRL_U,
    KEY_CTRL_V, KEY_CTRL_W, KEY_CTRL_X, KEY_CTRL_Y,
    KEY_CTRL_Z = 26,
    KEY_ESCAPE = 27,
    KEY_BACKSPACE = 127,
    /* Special keys encoded above 256 */
    KEY_ARROW_UP = 300,
    KEY_ARROW_DOWN,
    KEY_ARROW_LEFT,
    KEY_ARROW_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_DELETE,
    KEY_INSERT,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4,
    KEY_F5, KEY_F6, KEY_F7, KEY_F8,
    KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_CTRL_SHIFT_C = 400,
    KEY_CTRL_TAB,
    KEY_SHIFT_TAB,
    KEY_MOUSE = 500
} KeyCode;

/* ── Mouse event ── */
typedef struct {
    int button;  /* 0=left,1=middle,2=right,64=scroll-up,65=scroll-down */
    int x, y;   /* 1-based col/row */
    bool released;
} MouseEvent;

/* ── Input event ── */
typedef struct {
    KeyCode key;
    uint32_t codepoint; /* for printable chars */
    MouseEvent mouse;
    bool is_mouse;
} InputEvent;

/* ── Git file status ── */
typedef enum {
    GIT_STATUS_NONE = 0,
    GIT_STATUS_MODIFIED,
    GIT_STATUS_UNTRACKED,
    GIT_STATUS_STAGED,
    GIT_STATUS_RENAMED,
    GIT_STATUS_DELETED,
    GIT_STATUS_CONFLICT
} GitStatus;

/* ── Line in the text buffer ── */
typedef struct {
    char  *data;        /* UTF-8 text (not NUL-terminated necessarily) */
    int    len;         /* bytes used */
    int    cap;         /* bytes allocated */
    bool   dirty;       /* highlight cache invalid */
} Line;

/* ── Text buffer ── */
typedef struct {
    Line  *lines;
    int    num_lines;
    int    cap_lines;
    bool   modified;
    char   filepath[MAX_PATH];
    char   lang[32];   /* "c", "python", "json", "bash", "" */
} Buffer;

/* ── Tab ── */
typedef struct {
    Buffer  buf;
    int     cursor_row;   /* 0-based */
    int     cursor_col;   /* 0-based byte offset */
    int     scroll_row;
    int     scroll_col;
    bool    active;
    char    title[256];
} Tab;

/* ── File-explorer node ── */
typedef struct FileNode {
    char   name[256];
    char   path[MAX_PATH];
    bool   is_dir;
    bool   expanded;
    int    depth;
    GitStatus git_status;
} FileNode;

/* ── Chat message ── */
typedef struct {
    char  *text;
    bool   is_user; /* true=user, false=assistant */
} ChatMessage;

/* ── LSP state ── */
typedef struct {
    pid_t  pid;
    int    stdin_fd;
    int    stdout_fd;
    bool   initialized;
    int    req_id;
} LspState;

/* ── PTY terminal state ── */
typedef struct {
    pid_t  pid;
    int    master_fd;
    char   buf[65536];
    int    buf_len;
    bool   running;
    int    rows, cols;
} PtyState;

/* ── Pane geometry ── */
typedef struct {
    int x, y;      /* top-left (1-based) */
    int w, h;      /* width, height in chars */
    bool visible;
} PaneGeom;

/* ── Global app state ── */
typedef struct {
    /* Screen */
    int term_rows, term_cols;

    /* Panes */
    PaneGeom panes[PANE_COUNT];
    PaneId   active_pane;
    bool     explorer_visible;
    bool     terminal_visible;
    bool     chat_visible;

    /* Explorer */
    FileNode  explorer_nodes[MAX_EXPLORER_NODES];
    int       explorer_count;
    int       explorer_sel;
    int       explorer_scroll;
    char      cwd[MAX_PATH];

    /* Editor */
    Tab       tabs[MAX_TABS];
    int       tab_count;
    int       active_tab;
    EditorMode mode;
    char      cmd_buf[1024];    /* for ':' command or '/' search */
    int       cmd_len;
    char      status_msg[512];

    /* Chat */
    ChatMessage chat_msgs[MAX_CHAT_MSGS];
    int         chat_count;
    char        chat_input[1024];
    int         chat_input_len;
    int         chat_scroll;

    /* Terminal */
    PtyState   pty;

    /* LSP */
    LspState   lsp;

    /* Git */
    char  git_branch[128];
    bool  in_git_repo;

    /* Quit flag */
    bool  quit;
    bool  needs_redraw;
} AppState;

extern AppState g;

#endif /* YOTTA_TYPES_H */
