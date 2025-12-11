// jobs.h - background/foreground job table management and fg/bg builtins
#ifndef JOBS_H
#define JOBS_H
#include <sys/types.h>

// Poll background jobs for completion (printing exit messages)
void jobs_poll(void);

// Enumerate current activities (running or stopped pipeline stages)
int jobs_for_each_activity(int (*cb)(pid_t pid, const char *name, int stopped, void *ud), void *ud);

// Foreground bookkeeping (used by executor + signals)
void jobs_set_foreground(pid_t pgid, const pid_t *pids, int count, const char *name);
void jobs_clear_foreground(void);
int  jobs_get_foreground(pid_t *pgid_out, pid_t *pids_out, int max, char *name_buf, size_t name_buf_sz);
int  jobs_move_foreground_to_background_stopped(void); // returns job number or -1

// Register a new background job with given pids and per-stage names.
// Returns job number, fills last_pid_out with pid of last stage.
int jobs_add_background(const pid_t *pids, int count, const char *const *stage_names, pid_t *last_pid_out);

// Builtin helpers (return shell status codes)
int jobs_cmd_fg(int jobnum);
int jobs_cmd_bg(int jobnum);

#endif // JOBS_H
