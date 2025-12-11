// Mini shell entry point
// ----------------------
// This file implements the interactive REPL (read-eval-print loop) of the shell:
// - initialize modules (prompt, signals, history)
// - print a prompt
// - read a line from stdin
// - validate the syntax using the parser
// - store the command in history (with some rules)
// - execute the first command-group using the executor
//
// Key ideas to learn:
// - A shell is just a loop around fgets() + fork()/exec()/wait() (done by executor.c)
// - Job control: we give and take terminal control with tcsetpgrp() when
//   running foreground pipelines
// - SIGTTIN/SIGTTOU are ignored in the shell to avoid being stopped when
//   controlling the terminal foreground process group
//
// Ensure POSIX extensions for sigaction flags
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include "prompt.h"
#include "parser.h"
#include "hop.h"
#include "reveal.h"
#include "executor.h"
#include "jobs.h"
#include "signals.h"
#include "log.h"
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <time.h>

// Removed custom SIGCHLD reaper per request; background jobs reaped when polled.

// Callback to kill all remaining activities on Ctrl-D (EOF)
static int kill_activity_cb(pid_t pid, const char *name, int stopped, void *ud){
    (void)name; (void)stopped; (void)ud; // unused
    if(pid>0) kill(pid, SIGKILL);
    return 0;
}

int main(void) {
    prompt_init();
    signals_init();
    log_init();

    char input[1024];
    // No custom SIGCHLD handler; rely on polling in jobs/executor.

    // Ensure the shell isn't stopped by the terminal when switching foreground pgid
    // (tcsetpgrp from a background process sends SIGTTOU by default). Standard shells ignore these.
    struct sigaction ign; memset(&ign,0,sizeof(ign)); ign.sa_handler = SIG_IGN;
    sigaction(SIGTTOU, &ign, NULL);
    sigaction(SIGTTIN, &ign, NULL);

    // Place the shell in its own process group and take control of the terminal
    // so that foreground job handoff works and the shell doesn't get stopped.
    pid_t shell_pgid = getpid();
    // Ignore errors if already set
    setpgid(0, shell_pgid);
    tcsetpgrp(STDIN_FILENO, shell_pgid);

    while (1) {
        // Poll background jobs and process deferred signals
        executor_poll_background();
        signals_process_pending();
        if (executor_recent_stop()) {
            // tiny delay to let tty print and avoid race in test harness
            struct timespec ts = {0, 50 * 1000 * 1000}; // 50ms
            nanosleep(&ts, NULL);
        }
        prompt_print();

        if (!fgets(input, sizeof(input), stdin)) {
            if (feof(stdin)) {
                // EOF (Ctrl-D): kill remaining jobs, print logout, exit 0
                // Use \n; terminal will map to CRLF. Avoid writing \r\n directly to prevent \r\r\n on ONLCR ttys.
                fputs("logout\n", stdout);
                executor_for_each_activity(kill_activity_cb, NULL);
                return 0;
            }
            if (ferror(stdin)) { clearerr(stdin); continue; }
            continue; // defensive
        }
        // Immediately before executing the typed command, flush any job completion messages
        // so they appear before this command's output (expected by tests).
        executor_poll_background();
        signals_process_pending();
        // validate syntax (now builtins handled inside executor to support pipes/redirection)
        if (!parse_command(input)) {
            // Use \n; terminal line discipline will translate to CRLF for pty captures
            fputs("Invalid Syntax!\n", stdout);
            continue;
        }
        // Store the entire shell_cmd in history (subject to rules)
        log_maybe_store_shell_cmd(input);
        // Execute all command groups (executor handles builtins & background '&')
        (void)execute_first_cmd_group(input);
    }
    prompt_cleanup();
    return 0;
}
