#ifndef HOP_H
#define HOP_H

#include <stdbool.h>

// Try to handle a hop command line.
// input is the full line (e.g., "hop .. - /tmp\n").
// Returns true if the line was a hop command (handled or attempted), false otherwise.
// Prints "No such directory!" per requirements when a target doesn't exist.
bool try_handle_hop(const char *input);

// Handle a 'cd' command line. Supports: cd, cd ~, cd ., cd .., cd -, cd <path>.
// Prints errors similar to hop: "No such directory!" when target invalid.
// For more than one argument, prints: "cd: too many arguments" and handles nothing further.
bool try_handle_cd(const char *input);

// New: argv-based execution variants used by the executor so builtins can
// participate in redirection/pipelines. Return 0 on success, non-zero on error.
int run_hop_argv(int argc, char **argv);
int run_cd_argv(int argc, char **argv);

// Helpers used by 'reveal -' to refer to the previous directory.
// Return 1 if a previous CWD is known and usable; else 0.
int hop_prev_cwd_available(void);
// Returns a const pointer to the previous CWD string if available, otherwise NULL.
const char* hop_get_prev_cwd(void);

#endif // HOP_H
