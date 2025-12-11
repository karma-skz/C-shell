#ifndef PING_H
#define PING_H
#include <stdbool.h>
// argv-based handler: ping <pid> <signal_number>
// Returns 0 on success, non-zero on error.
int run_ping_argv(int argc, char **argv);
// Line-based quick detection (optional, not used now)
bool try_handle_ping(const char *line);
#endif // PING_H
