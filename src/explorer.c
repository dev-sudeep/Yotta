#include "explorer.h"
#include "editor.h"
#include "git.h"
#include "types.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Hidden dirs/files to skip ── */
static const char *SKIP[] = {
    ".git", "node_modules", "__pycache__", ".DS_Store",
    "build", "dist", ".cache", NULL
};

static bool should_skip(const char *name) {
    for (int i = 0; SKIP[i]; i++)
        if (strcmp(name, SKIP[i]) == 0) return true;
    return name[0] == '.'; /* skip hidden */
}

/* ── Flat array of visible nodes ── */
/* We store a tree-like structure: each node has a parent index (-1 for root).
   expand/collapse toggles whether its children appear in the flat list. */

/* Sorted directory listing into flat node array */
static int fill_dir(const char *dirpath, int depth) {
    if (g.explorer_count >= MAX_EXPLORER_NODES - 1) return 0;
    if (depth > 16) return 0;  /* sanity */

    DIR *dp = opendir(dirpath);
    if (!dp) return 0;

    /* Read all entries, sort: dirs first, then files */
    char names[512][256];
    bool is_dir[512];
    int nc = 0;

    struct dirent *de;
    while ((de = readdir(dp)) && nc < 512) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (should_skip(de->d_name)) continue;
        strncpy(names[nc], de->d_name, 255);
        /* stat to determine dir */
        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", dirpath, de->d_name);
        struct stat st;
        if (stat(full, &st) == 0)
            is_dir[nc] = S_ISDIR(st.st_mode);
        else
            is_dir[nc] = (de->d_type == DT_DIR);
        nc++;
    }
    closedir(dp);

    /* Stable sort: dirs first, then alphabetical */
    for (int i = 0; i < nc - 1; i++) {
        for (int j = i + 1; j < nc; j++) {
            bool swap = false;
            if (is_dir[j] && !is_dir[i]) swap = true;
            else if (is_dir[j] == is_dir[i] &&
                     strcmp(names[i], names[j]) > 0) swap = true;
            if (swap) {
                char tmp[256]; memcpy(tmp, names[i], 256); memcpy(names[i], names[j], 256); memcpy(names[j], tmp, 256);
                bool bt = is_dir[i]; is_dir[i] = is_dir[j]; is_dir[j] = bt;
            }
        }
    }

    for (int i = 0; i < nc && g.explorer_count < MAX_EXPLORER_NODES; i++) {
        FileNode *n = &g.explorer_nodes[g.explorer_count++];
        strncpy(n->name, names[i], sizeof(n->name) - 1);
        snprintf(n->path, sizeof(n->path), "%s/%s", dirpath, names[i]);
        n->is_dir   = is_dir[i];
        n->expanded = false;
        n->depth    = depth;
        n->git_status = git_file_status(n->path);

        /* Recursively add children if expanded */
        if (is_dir[i] && n->expanded)
            fill_dir(n->path, depth + 1);
    }
    return nc;
}

void explorer_init(const char *dir) {
    strncpy(g.cwd, dir, MAX_PATH - 1);
    explorer_refresh();
}

void explorer_refresh(void) {
    g.explorer_count = 0;
    g.explorer_sel   = 0;
    g.explorer_scroll = 0;
    fill_dir(g.cwd, 0);
    g.needs_redraw = true;
}

/* Re-build the flat list preserving expanded state */
static void rebuild(void) {
    /* Save expanded state */
    typedef struct { char path[MAX_PATH]; bool expanded; } SavedState;
    SavedState saved[MAX_EXPLORER_NODES];
    int nsaved = 0;
    for (int i = 0; i < g.explorer_count && nsaved < MAX_EXPLORER_NODES; i++) {
        if (g.explorer_nodes[i].expanded) {
            strncpy(saved[nsaved].path, g.explorer_nodes[i].path, MAX_PATH - 1);
            saved[nsaved].expanded = true;
            nsaved++;
        }
    }

    g.explorer_count = 0;

    /* Rebuild recursively but respecting saved expanded state */
    /* For simplicity, re-fill and mark expanded */
    fill_dir(g.cwd, 0);

    for (int i = 0; i < g.explorer_count; i++) {
        for (int j = 0; j < nsaved; j++) {
            if (strcmp(g.explorer_nodes[i].path, saved[j].path) == 0) {
                g.explorer_nodes[i].expanded = true;
            }
        }
    }

    /* Now re-expand dirs that are marked expanded */
    /* We need a second pass inserting children — simplest: recursive rebuild */
    /* For this implementation we rebuild the flat list by doing a DFS */
    /* Reset and do DFS */
    g.explorer_count = 0;

    /* DFS stack */
    typedef struct { char path[MAX_PATH]; int depth; bool expand; } StackFrame;
    StackFrame stack[MAX_EXPLORER_NODES];
    int sp = 0;
    /* Push cwd's children in reverse so first comes out first */
    {
        /* First collect cwd children */
        DIR *dp = opendir(g.cwd);
        if (!dp) return;
        char names[512][256]; bool is_dirs[512]; int nc = 0;
        struct dirent *de;
        while ((de = readdir(dp)) && nc < 512) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            if (should_skip(de->d_name)) continue;
            strncpy(names[nc], de->d_name, 255);
            char full[MAX_PATH]; snprintf(full, sizeof(full), "%s/%s", g.cwd, de->d_name);
            struct stat st;
            is_dirs[nc] = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
            nc++;
        }
        closedir(dp);
        /* Sort: dirs first */
        for (int i = 0; i < nc - 1; i++)
            for (int j = i + 1; j < nc; j++) {
                bool sw = (is_dirs[j] && !is_dirs[i]) ||
                          (is_dirs[j] == is_dirs[i] && strcmp(names[i], names[j]) > 0);
                if (sw) {
                    char t2[256]; memcpy(t2, names[i], 256); memcpy(names[i], names[j], 256); memcpy(names[j], t2, 256);
                    bool bt = is_dirs[i]; is_dirs[i] = is_dirs[j]; is_dirs[j] = bt;
                }
            }
        for (int i = nc - 1; i >= 0 && sp < MAX_EXPLORER_NODES; i--) {
            snprintf(stack[sp].path, MAX_PATH, "%s/%s", g.cwd, names[i]);
            stack[sp].depth = 0;
            /* check if saved as expanded */
            bool exp = false;
            for (int j = 0; j < nsaved; j++)
                if (strcmp(stack[sp].path, saved[j].path) == 0) { exp = true; break; }
            stack[sp].expand = exp && is_dirs[i];
            sp++;
        }
    }

    while (sp > 0 && g.explorer_count < MAX_EXPLORER_NODES) {
        sp--;
        StackFrame *fr = &stack[sp];
        FileNode *n = &g.explorer_nodes[g.explorer_count++];
        /* get basename */
        const char *slash = strrchr(fr->path, '/');
        strncpy(n->name, slash ? slash + 1 : fr->path, sizeof(n->name) - 1);
        strncpy(n->path, fr->path, sizeof(n->path) - 1);
        n->depth = fr->depth;
        n->git_status = git_file_status(fr->path);

        struct stat st;
        n->is_dir = (stat(fr->path, &st) == 0 && S_ISDIR(st.st_mode));
        n->expanded = fr->expand;

        if (n->is_dir && n->expanded) {
            /* push children in reverse */
            DIR *dp = opendir(fr->path);
            if (dp) {
                char cnames[512][256]; bool cdirs[512]; int cnc = 0;
                struct dirent *de;
                while ((de = readdir(dp)) && cnc < 512) {
                    if (strcmp(de->d_name,".")==0||strcmp(de->d_name,"..")==0) continue;
                    if (should_skip(de->d_name)) continue;
                    strncpy(cnames[cnc], de->d_name, 255);
                    char full[MAX_PATH]; snprintf(full, MAX_PATH, "%s/%s", fr->path, de->d_name);
                    struct stat st2;
                    cdirs[cnc] = (stat(full, &st2) == 0 && S_ISDIR(st2.st_mode));
                    cnc++;
                }
                closedir(dp);
                for (int ii = 0; ii < cnc - 1; ii++)
                    for (int jj = ii + 1; jj < cnc; jj++) {
                        bool sw = (cdirs[jj] && !cdirs[ii]) ||
                                  (cdirs[jj]==cdirs[ii] && strcmp(cnames[ii],cnames[jj])>0);
                        if (sw) {
                            char t2[256]; memcpy(t2,cnames[ii],256); memcpy(cnames[ii],cnames[jj],256); memcpy(cnames[jj],t2,256);
                            bool bt=cdirs[ii]; cdirs[ii]=cdirs[jj]; cdirs[jj]=bt;
                        }
                    }
                for (int ii = cnc - 1; ii >= 0 && sp < MAX_EXPLORER_NODES - 1; ii--) {
                    snprintf(stack[sp].path, MAX_PATH, "%s/%s", fr->path, cnames[ii]);
                    stack[sp].depth = fr->depth + 1;
                    bool exp = false;
                    for (int j = 0; j < nsaved; j++)
                        if (strcmp(stack[sp].path, saved[j].path)==0) { exp=true; break; }
                    stack[sp].expand = exp && cdirs[ii];
                    sp++;
                }
            }
        }
    }

    g.needs_redraw = true;
}

void explorer_toggle(void) {
    if (g.explorer_sel < 0 || g.explorer_sel >= g.explorer_count) return;
    FileNode *n = &g.explorer_nodes[g.explorer_sel];
    if (!n->is_dir) return;
    n->expanded = !n->expanded;
    rebuild();
}

void explorer_move_up(void) {
    if (g.explorer_sel > 0) {
        g.explorer_sel--;
        if (g.explorer_sel < g.explorer_scroll)
            g.explorer_scroll = g.explorer_sel;
        g.needs_redraw = true;
    }
}

void explorer_move_down(void) {
    if (g.explorer_sel + 1 < g.explorer_count) {
        g.explorer_sel++;
        int vis_h = g.panes[PANE_EXPLORER].h - 1;
        if (g.explorer_sel >= g.explorer_scroll + vis_h)
            g.explorer_scroll = g.explorer_sel - vis_h + 1;
        g.needs_redraw = true;
    }
}

void explorer_open_selected(void) {
    if (g.explorer_sel < 0 || g.explorer_sel >= g.explorer_count) return;
    FileNode *n = &g.explorer_nodes[g.explorer_sel];
    if (n->is_dir) {
        explorer_toggle();
    } else {
        editor_open_file(n->path);
        g.active_pane = PANE_EDITOR;
        g.needs_redraw = true;
    }
}

void explorer_handle_event(InputEvent *ev) {
    switch (ev->key) {
        case KEY_ARROW_UP:   explorer_move_up();       break;
        case KEY_ARROW_DOWN: explorer_move_down();     break;
        case KEY_ENTER:      explorer_open_selected(); break;
        case KEY_CTRL_R:     explorer_refresh();       break;
        default:
            if (ev->codepoint) {
                switch (ev->codepoint) {
                    case 'j': explorer_move_down();     break;
                    case 'k': explorer_move_up();       break;
                    case 'l':
                    case '\n': explorer_open_selected(); break;
                    case 'h': explorer_toggle();        break;
                    case 'r': explorer_refresh();       break;
                    default: break;
                }
            }
            break;
    }
}
