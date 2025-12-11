#ifndef REVEAL_H
#define REVEAL_H

#include <stdbool.h>

// Handle a 'reveal' command line.
// Returns true if the input started with 'reveal' and was handled (even if it prints an error), false otherwise.
bool try_handle_reveal(const char *input);

// argv-based execution for pipeline/redirection integration.
int run_reveal_argv(int argc, char **argv);

#endif // REVEAL_H
