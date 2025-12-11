// signals.c: tiny signal helpers
// ------------------------------
// For learning purposes, this module currently keeps signal handling simple:
// - signals_init() does nothing
// - signals_process_pending() does nothing
// - signals_reset_for_child() leaves default dispositions in child processes
// The executor carefully moves jobs into their own process group so Ctrl-C/Z
// from the terminal affect foreground jobs, not the shell.
//
// When you extend this, you might forward SIGINT/SIGTSTP to the foreground
// job group or remember pending signals to handle safely in the main loop.
// For now, keeping this a no-op reduces moving parts while you learn.
//
// Signals module now inert per user request: no custom Ctrl+C/Z/D handling.
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "signals.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void handle_sigint(int sig) {
    (void)sig;
    // Write a newline so the prompt doesn't get messed up
    write(STDOUT_FILENO, "\n", 1);
}

void signals_init(void) {
    struct sigaction sa;
    
    // Handle SIGINT (Ctrl-C)
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    // We do NOT use SA_RESTART so that blocking calls like fgets() return immediately
    // with errno=EINTR, allowing the main loop to reprint the prompt.
    sa.sa_flags = 0; 
    sigaction(SIGINT, &sa, NULL);

    // Ignore SIGTSTP (Ctrl-Z) - the shell shouldn't be stopped
    struct sigaction sa_ign;
    memset(&sa_ign, 0, sizeof(sa_ign));
    sa_ign.sa_handler = SIG_IGN;
    sigemptyset(&sa_ign.sa_mask);
    sa_ign.sa_flags = 0;
    sigaction(SIGTSTP, &sa_ign, NULL);
}

void signals_process_pending(void) {
    // No-op for now, as we handle SIGINT directly in the handler
    // or rely on EINTR to wake up the main loop.
}

void signals_reset_for_child(void) {
    struct sigaction sa_dfl;
    memset(&sa_dfl, 0, sizeof(sa_dfl));
    sa_dfl.sa_handler = SIG_DFL;
    sigemptyset(&sa_dfl.sa_mask);
    sa_dfl.sa_flags = 0;
    
    sigaction(SIGINT, &sa_dfl, NULL);
    sigaction(SIGTSTP, &sa_dfl, NULL);
    // Reset SIGTTOU/SIGTTIN as well since they are ignored in main.c
    sigaction(SIGTTOU, &sa_dfl, NULL);
    sigaction(SIGTTIN, &sa_dfl, NULL);
}
