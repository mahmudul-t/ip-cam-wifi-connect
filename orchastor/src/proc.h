// ========================= src/proc.h =========================
#ifndef PROC_H
#define PROC_H
#include <sys/types.h>

typedef struct { pid_t pid; pid_t pgid; } child_t;

int  spawn_sh(const char *script_path, const char *logfile, child_t *out);
int  spawn_exec(const char *path, char *const argv[], const char *logfile, child_t *out);
void kill_group(pid_t pgid, const char *what);
void pkill_like(const char *needle, const char *tag);

#endif
