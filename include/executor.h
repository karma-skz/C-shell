#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stdbool.h>
#include <sys/types.h>

// Execute all command groups separated by ';' or '&' (background '&' treated like ';').
// Supports pipelines with '|', input '<' and output '>'/ '>>' redirection.
// Builtins hop, cd, reveal are integrated so they work with redirection/pipes; when
// used without redirection/piping they run in-process to preserve state.
// Returns status of the last command group.
int execute_first_cmd_group(const char *line);

// Check and report completed background jobs; call before reading new input.
void executor_poll_background(void);

// Enumerate current background process stages that are not finished.
// Callback receives (pid, name, stopped_flag). Returns number of entries passed.
int executor_for_each_activity(int (*cb)(pid_t pid, const char *name, int stopped, void *ud), void *ud);

// True once immediately after a foreground job was stopped (Ctrl-Z),
// then resets to 0 on read. Used by main loop to add a tiny delay
// before printing the next prompt to avoid racey interleaving.
int executor_recent_stop(void);

// Job control APIs moved to jobs.h (executor now only executes pipelines & delegates job mgmt)

#endif // EXECUTOR_H
