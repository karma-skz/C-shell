#ifndef PROMPT_H
#define PROMPT_H

void prompt_init(void); // Init username, hostname, and the shell's "home" (CWD at startup)
void prompt_print(void); // Print "<user@host:path>" followed by a single space (no newline)
void prompt_cleanup(void); // Clean up any heap allocations used by the prompt module

// Returns the shell's home directory as determined at startup.
// Never free this pointer. May return NULL in pathological cases.
const char* prompt_home(void);
#endif