#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

// Returns 1 if the input string is a valid command per the grammar, else 0.
// Whitespace between tokens (space, tab, CR, LF) is ignored.
int parse_command(const char *s);

#endif // PARSER_H
