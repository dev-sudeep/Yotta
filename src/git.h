#ifndef YOTTA_GIT_H
#define YOTTA_GIT_H

#include "types.h"

/* Detect if cwd is inside a git repo; sets g.in_git_repo and g.git_branch */
void git_detect(const char *dir);

/* Get the git status for a single file path */
GitStatus git_file_status(const char *filepath);

/* Refresh git status for all explorer nodes */
void git_refresh_status(void);

/* Stage a file */
bool git_stage(const char *filepath);

/* Unstage a file */
bool git_unstage(const char *filepath);

/* Commit with a message */
bool git_commit(const char *message);

/* Get diff of a file (returns malloc'd string, caller frees) */
char *git_diff(const char *filepath);

#endif /* YOTTA_GIT_H */
