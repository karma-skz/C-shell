#ifndef LOG_H
#define LOG_H

#include <stdbool.h>

// Initialize history from persistent storage
void log_init(void);

// Consider storing the given shell_cmd (entire line) according to rules:
// - Do not store if identical to the immediately previous stored command
// - Do not store if any atomic command name is "log"
// - Store at most 15, overwriting oldest
void log_maybe_store_shell_cmd(const char *line);

// Builtin entrypoint: implements
//   log
//   log purge
//   log execute <index>
// Returns a shell status code.
int run_log_argv(int argc, char **argv);

#endif // LOG_H
