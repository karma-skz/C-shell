#ifndef SIGNALS_H
#define SIGNALS_H
#include <sys/types.h>
#include <signal.h>

void signals_init(void);
void signals_process_pending(void);
void signals_reset_for_child(void);

#endif // SIGNALS_H
