// executor.c: run commands and pipelines
// --------------------------------------
// This module turns a validated input line into processes using fork/exec.
// It also implements a tiny job control so Ctrl-C/Z affect only the foreground
// job. To keep things digestible for beginners we:
// - parse the first command-group ourselves (very small tokenizer)
// - implement simple pipelines with '|'
// - support basic redirections: <, >, >> (both attached and spaced forms)
// - run known builtins without exec (they can also run in child when piped)
// - assign a process group to pipelines and hand over the terminal to them
//
// Reading guidance:
// 1) Data structures at the top (SimpleCmd, Pipeline, Redir)
// 2) Small parsing helpers (read_name, parse_segment, parse_pipeline)
// 3) Running a pipeline in foreground (run_pipeline)
// 4) Running a pipeline in background (run_pipeline_async)
// 5) Glue that walks command-groups separated by ;, &, &&

#include "executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include "signals.h"
#include <unistd.h>
#include <time.h>

#define MAX_CMDS 16   // up to 16 commands in a single pipeline
#define MAX_ARGS 64   // up to 64 arguments per command (including argv[0])
#define MAX_REDIRS 16 // up to 16 redirections per command

typedef enum { R_IN = 0, R_OUT_TRUNC = 1, R_OUT_APPEND = 2 } RedirType;

typedef struct {
    RedirType type;
    char *path;
} Redir;

typedef struct {
    char *argv[MAX_ARGS]; // NULL-terminated
    Redir redirs[MAX_REDIRS];
    int redir_count;
} SimpleCmd;

typedef struct {
    SimpleCmd cmds[MAX_CMDS];
    int count; // number of commands in the pipeline
} Pipeline;

#include "jobs.h"
static char last_fg_name[128];
static volatile int g_recent_stop = 0;

static const char* skip_ws(const char *p){
    while (*p==' ' || *p=='\t' || *p=='\n' || *p=='\r') p++;
    return p;
}

static char *dup_range(const char *start, size_t len){
    char *s = (char*)malloc(len+1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

// Legacy helper removed; multi-group handling implemented below.

// Read a "name" token: stops at whitespace or special "|&;<>"
static char* read_name(const char **pp){
    const char *p = *pp;
    size_t i = 0;
    while (p[i] && p[i] != ' ' && p[i] != '\t' && p[i] != '\n' && p[i] != '\r' &&
           p[i] != '|' && p[i] != '<' && p[i] != '>' && p[i] != '&' && p[i] != ';') {
        i++;
    }
    if (i==0) return NULL;
    char *tok = dup_range(p, i);
    *pp = p + i;
    return tok;
}

// Parse one command segment (no pipes inside): argv + optional redirections
static int parse_segment(const char *seg, SimpleCmd *cmd){
    memset(cmd, 0, sizeof(*cmd));
    int argc = 0;
    const char *p = seg;
    p = skip_ws(p);
    // First token must be the program name
    char *tok = read_name(&p);
    if (!tok) return 0;
    cmd->argv[argc++] = tok;

    // Other tokens: args or redirections (<, >, >>) in any order
    for (;;) {
        p = skip_ws(p);
        if (*p == '\0') break;
        if (*p == '|') break; // caller splits on '|'

        if (*p == '<' || *p == '>') {
            int is_in = (*p == '<');
            int is_append = 0;
            p++;
            if (!is_in && *p == '>') { is_append = 1; p++; }
            p = skip_ws(p);
            char *fname = NULL;
            // Attached form like <file or >>file
            if (*p && *p!=' ' && *p!='\t' && *p!='\n' && *p!='\r' && *p!='|' && *p!='<' && *p!='>' && *p!='&' && *p!=';') {
                fname = read_name(&p);
            }
            if (!fname) { // spaced form: < file
                fname = read_name(&p);
            }
            if (!fname) {
                fprintf(stderr, "redirection: missing file name\n");
                return 0;
            }
            if (cmd->redir_count >= MAX_REDIRS) {
                fprintf(stderr, "too many redirections (max %d)\n", MAX_REDIRS);
                free(fname);
                return 0;
            }
            Redir *r = &cmd->redirs[cmd->redir_count++];
            r->type = is_in ? R_IN : (is_append ? R_OUT_APPEND : R_OUT_TRUNC);
            r->path = fname;
            continue;
        }

        // Normal argument
        tok = read_name(&p);
        if (!tok) break;
        if (argc < MAX_ARGS-1) {
            cmd->argv[argc++] = tok;
        } else {
            fprintf(stderr, "too many arguments (max %d)\n", MAX_ARGS-1);
            free(tok);
            return 0;
        }
    }
    cmd->argv[argc] = NULL;
    return 1;
}

// Parse a pipeline: split by '|' and parse each segment
static int parse_pipeline(const char *first, Pipeline *out){
    memset(out, 0, sizeof(*out));
    const char *p = first;
    while (*p) {
        // Find next '|' or end
        const char *seg_start = p;
        while (*p && *p != '|') p++;
        const char *seg_end = p; // points to '|' or '\0'
        // Trim whitespace on the segment
        while (seg_start < seg_end && (*seg_start==' '||*seg_start=='\t'||*seg_start=='\n'||*seg_start=='\r')) seg_start++;
        while (seg_end > seg_start && (seg_end[-1]==' '||seg_end[-1]=='\t'||seg_end[-1]=='\n'||seg_end[-1]=='\r')) seg_end--;
        if (seg_start==seg_end) return 0; // empty segment like "a || b"
        if (out->count >= MAX_CMDS) { fprintf(stderr, "too many pipeline stages (max %d)\n", MAX_CMDS); return 0; }
        char *seg = dup_range(seg_start, (size_t)(seg_end - seg_start));
        if (!seg) return 0;
        if (!parse_segment(seg, &out->cmds[out->count])) { free(seg); return 0; }
        free(seg);
        out->count++;
        if (*p == '|') {
            // Look ahead to ensure another non-whitespace token follows; otherwise it's a trailing pipe -> invalid
            const char *la = p+1;
            while (*la==' '||*la=='\t'||*la=='\n'||*la=='\r') la++;
            if (*la == '\0') return 0; // trailing pipe
            p++; // skip '|'
        }
        // continue loop
    }
    return (out->count > 0);
}

static void free_pipeline(Pipeline *pl){
    for (int i=0;i<pl->count;i++) {
        SimpleCmd *c = &pl->cmds[i];
        for (int j=0; c->argv[j]; j++) free(c->argv[j]);
        for (int r=0; r<c->redir_count; r++) {
            free(c->redirs[r].path);
        }
        memset(c, 0, sizeof(*c));
    }
    pl->count = 0;
}

// Forward declare builtin helper used inside run_pipeline (defined later)
static int run_builtin(SimpleCmd *c);

static int run_pipeline(Pipeline *pl){
    int n = pl->count;
    if (n <= 0) return 1;
    pid_t pids[MAX_CMDS];
    for (int i=0;i<n;i++) pids[i] = -1;
    pid_t pgid = -1;

    int prev_read = -1;
    int status_code = 0;

    for (int i=0;i<n;i++) {
        int pipefd[2] = {-1,-1};
        if (i < n-1) {
            if (pipe(pipefd) < 0) { perror("pipe"); status_code = 1; break; }
        }
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); status_code = 1; break; }
        if (pid == 0) {
            // Child
            // Put each child into the process group of first child
            if (pgid == -1) pgid = getpid();
            if (setpgid(0, pgid) < 0) {
                // Can't safely fprintf after fork before exec? we are ok minimal
            }
            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
            }
            if (pipefd[1] != -1) {
                dup2(pipefd[1], STDOUT_FILENO);
            }
            // Reset signals to default in the child so the terminal can deliver
            // Ctrl-C (SIGINT) / Ctrl-Z (SIGTSTP) to the foreground job, not caught by the shell.
            signals_reset_for_child();
            // Redirections override pipes; apply left-to-right
            SimpleCmd *c = &pl->cmds[i];
            for (int ri = 0; ri < c->redir_count; ri++) {
                Redir *r = &c->redirs[ri];
                if (r->type == R_IN) {
                    int fd = open(r->path, O_RDONLY);
                    if (fd < 0) { fprintf(stderr, "No such file or directory\n"); _exit(1); }
                    dup2(fd, STDIN_FILENO); close(fd);
                } else {
                    int flags = O_WRONLY | O_CREAT | ((r->type==R_OUT_APPEND) ? O_APPEND : O_TRUNC);
                    int fd = open(r->path, flags, 0644);
                    if (fd < 0) { fputs("Unable to create file for writing\n", stderr); _exit(1); }
                    dup2(fd, STDOUT_FILENO); close(fd);
                }
            }
            // Close fds in child
            if (prev_read != -1) close(prev_read);
            if (pipefd[0] != -1) close(pipefd[0]);
            if (pipefd[1] != -1) close(pipefd[1]);
            // Builtin? Run directly then exit the child with its return code.
            int b = run_builtin(c);
            if (b != -1) {
                _exit(b);
            }
            execvp(c->argv[0], c->argv);
            // Standardize unknown command error message for tests
            fputs("Command not found!\n", stderr);
            _exit(127);
        }
        // Parent
        pids[i] = pid;
        if (pgid == -1) pgid = pid; // first child pid becomes pgid
        // Set child's process group
        if (setpgid(pid, pgid) < 0 && errno != EACCES && errno != ESRCH) {
            // ignore errors where child already exec'd
        }
        if (prev_read != -1) close(prev_read);
        if (pipefd[1] != -1) close(pipefd[1]);
        prev_read = pipefd[0];
    }

    if (prev_read != -1) close(prev_read);

    // Record foreground job and give the terminal to its process group.
    jobs_set_foreground(pgid, pids, n, pl->cmds[0].argv[0] ? pl->cmds[0].argv[0] : "?");
    // store name locally for message after move
    strncpy(last_fg_name, pl->cmds[0].argv[0]?pl->cmds[0].argv[0]:"?", sizeof(last_fg_name)-1); last_fg_name[sizeof(last_fg_name)-1]='\0';
    // Give terminal to foreground pgid
    tcsetpgrp(STDIN_FILENO, pgid);

    int stopped = 0;
    // Wait for each stage. If any stage is stopped, we later move the whole
    // pipeline to background as a stopped job and print a message.
    for (int i=0;i<pl->count;i++) {
        if (pids[i] > 0) {
            int st = 0; if (waitpid(pids[i], &st, WUNTRACED) > 0) {
                if (WIFSTOPPED(st)) {
                    stopped = 1; // mark
                } else if (i == pl->count - 1) {
                    if (WIFEXITED(st)) status_code = WEXITSTATUS(st); else status_code = 1;
                }
            }
        }
    }
    // If any stopped, move foreground to background as stopped job
    if (stopped) {
        g_recent_stop = 1;
        int jobnum = jobs_move_foreground_to_background_stopped();
        if (jobnum != -1) {
            printf("[%d] Stopped %s\n", jobnum, last_fg_name[0]?last_fg_name:"?");
            fflush(stdout);
        }
        // Reclaim terminal control for the shell after moving job to background
        tcsetpgrp(STDIN_FILENO, getpgrp());
        jobs_clear_foreground();
    } else {
        // Foreground pipeline completed: restore terminal control to the shell
        tcsetpgrp(STDIN_FILENO, getpgrp());
        jobs_clear_foreground();
    }
    return stopped ? 148 : status_code; // 148 arbitrary for stopped foreground
}

// Builtin integration
#include "hop.h"
#include "reveal.h"
#include "ping.h"
#include "log.h"

static int count_argv(SimpleCmd *c){ int i=0; while (c->argv[i]) i++; return i; }

static int run_builtin(SimpleCmd *c) {
    if (!c->argv[0]) return -1;
    if (strcmp(c->argv[0], "hop")==0) return run_hop_argv(count_argv(c), c->argv);
    if (strcmp(c->argv[0], "cd")==0) return run_cd_argv(count_argv(c), c->argv);
    if (strcmp(c->argv[0], "reveal")==0) return run_reveal_argv(count_argv(c), c->argv);
    if (strcmp(c->argv[0], "ping")==0) return run_ping_argv(count_argv(c), c->argv);
    if (strcmp(c->argv[0], "log")==0) return run_log_argv(count_argv(c), c->argv);
    if (strcmp(c->argv[0], "activities")==0) { extern int run_activities_argv(int argc, char **argv); return run_activities_argv(count_argv(c), c->argv); }
    if (strcmp(c->argv[0], "fg")==0) { int jobnum=0; if(c->argv[1]) jobnum=atoi(c->argv[1]); return jobs_cmd_fg(jobnum); }
    if (strcmp(c->argv[0], "bg")==0) { int jobnum=0; if(c->argv[1]) jobnum=atoi(c->argv[1]); return jobs_cmd_bg(jobnum); }
    return -1;
}

// Fork pipeline asynchronously (no waiting). Records pids into BgJob.
static int run_pipeline_async(Pipeline *pl, const char *segment_text) {
    if (pl->count <= 0) return 1;
    pid_t pids[MAX_CMDS];
    const char *names[MAX_CMDS];
    char *display_alloc = NULL;

    int n = pl->count;
    int prev_read = -1;
    pid_t pgid = -1;
    for (int i=0;i<n;i++) {
        int pipefd[2] = {-1,-1};
        if (i < n-1) {
            if (pipe(pipefd) < 0) { perror("pipe"); break; }
        }
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); break; }
        if (pid == 0) {
            // Child process in background: detach stdin if not redirected
            signals_reset_for_child();
            SimpleCmd *c = &pl->cmds[i];
            if (pgid == -1) pgid = getpid();
            setpgid(0, pgid);
            if (prev_read != -1) dup2(prev_read, STDIN_FILENO);
            if (pipefd[1] != -1) dup2(pipefd[1], STDOUT_FILENO);
            if (c->redir_count == 0) {
                // Ensure background jobs don't read from the terminal; connect
                // stdin to /dev/null unless input redirection was specified.
                int devnull = open("/dev/null", O_RDONLY);
                if (devnull >=0) { dup2(devnull, STDIN_FILENO); close(devnull);}            }
            // Apply redirections left-to-right (also ensures stdin detached if not redirected above)
            for (int ri = 0; ri < c->redir_count; ri++) {
                Redir *r = &c->redirs[ri];
                if (r->type == R_IN) {
                    int fd = open(r->path, O_RDONLY);
                    if (fd < 0) { fprintf(stderr, "No such file or directory\n"); _exit(1); }
                    dup2(fd, STDIN_FILENO); close(fd);
                } else {
                    int flags = O_WRONLY | O_CREAT | ((r->type==R_OUT_APPEND) ? O_APPEND : O_TRUNC);
                    int fd = open(r->path, flags, 0644);
                    if (fd < 0) { fputs("Unable to create file for writing\n", stderr); _exit(1); }
                    dup2(fd, STDOUT_FILENO); close(fd);
                }
            }
            if (prev_read != -1) close(prev_read);
            if (pipefd[0] != -1) close(pipefd[0]);
            if (pipefd[1] != -1) close(pipefd[1]);
            int b = run_builtin(c);
            if (b != -1) _exit(b);
            execvp(c->argv[0], c->argv);
            // Standardize unknown command error message for tests
            fputs("Command not found!\n", stderr);
            _exit(127);
        }
        if (pgid == -1) {
            pgid = pid;
        }
        setpgid(pid, pgid);
        pids[i] = pid;
        // Default stage name is argv[0]; we'll override names[0] with a nicer display below
        names[i] = pl->cmds[i].argv[0]?pl->cmds[i].argv[0]:segment_text;
            if (prev_read != -1) close(prev_read);
        if (pipefd[1] != -1) close(pipefd[1]);
        prev_read = pipefd[0];
    }
    if (prev_read != -1) close(prev_read);
    // Build a user-facing display for the whole job: for a single command, join
    // argv and append " &" so it matches what's usually typed.
    if (pl->count == 1) {
        SimpleCmd *c0 = &pl->cmds[0];
        size_t len = 0; int ac = 0; while (c0->argv[ac]) { len += strlen(c0->argv[ac]) + 1; ac++; }
        if (ac > 0) {
            display_alloc = (char*)malloc(len + 3); // space for ' &' and NUL
            if (display_alloc) {
                display_alloc[0] = '\0';
                for (int k=0;k<ac;k++) {
                    if (k) strcat(display_alloc, " ");
                    strcat(display_alloc, c0->argv[k]);
                }
                strcat(display_alloc, " &");
                names[0] = display_alloc;
            }
        }
    }
    pid_t lastpid=0; int jobnum = jobs_add_background(pids, pl->count, names, &lastpid);
    if (display_alloc) { free(display_alloc); display_alloc = NULL; }
    if(jobnum!=-1){ printf("[%d] %d\n", jobnum, (int)lastpid); fflush(stdout); }
    return 0;
}

// Poll background jobs for completion; print messages when done.
void executor_poll_background(void) { jobs_poll(); }

int executor_for_each_activity(int (*cb)(pid_t pid, const char *name, int stopped, void *ud), void *ud){ return jobs_for_each_activity(cb, ud); }

int executor_recent_stop(void) {
    int v = g_recent_stop;
    g_recent_stop = 0;
    return v;
}

// Internal: find job index by job number; returns -1 if not found

// Execute all command groups separated by ';', '&', and '&&'.
// '&' runs the previous segment as a background job (doesn't change last_status).
// '&&' runs the next segment only if the previous one succeeded (status 0).
int execute_first_cmd_group(const char *line){
    if (!line) return 1;
    const char *p = line;
    int last_status = 0;
    while (*p) {
        const char *start = p;
        // scan to next delimiter recognizing '&&' vs '&'
        const char *end = p;
        char delim1 = '\0'; // ';' or '&' or 'A' to denote '&&'
        while (*end) {
            if (*end == ';') { delim1 = ';'; break; }
            if (*end == '&') {
                if (end[1] == '&') { delim1 = 'A'; break; } // 'A' stands for AND
                else { delim1 = '&'; break; }
            }
            end++;
        }
        while (start < end && (*start==' '||*start=='\t'||*start=='\n'||*start=='\r')) start++;
        while (end>start && (end[-1]==' '||end[-1]=='\t'||end[-1]=='\n'||end[-1]=='\r')) end--;
        if (start==end) { if (*p) { p++; continue; } else break; }
        char *segment = dup_range(start, (size_t)(end-start));
        if (!segment) break;
        char delim = delim1; // ';', '&', 'A' (for &&), or '\0'
        Pipeline pl; if (parse_pipeline(segment, &pl)) {
            int is_background = (delim == '&');
            if (pl.count==1 && !is_background) {
                SimpleCmd *sc=&pl.cmds[0];
                int b = run_builtin(sc);
                if (b != -1 && sc->redir_count == 0) {
                    // Run directly (no fork) when no redirection/pipes needed.
                    last_status = b;
                } else {
                    last_status = run_pipeline(&pl);
                }
            } else {
                if (is_background) {
                    run_pipeline_async(&pl, segment);
                    // Do not update last_status (leave previous) per typical shell semantics
                } else {
                    last_status = run_pipeline(&pl);
                }
            }
            free_pipeline(&pl);
        } else {
            puts("Invalid Syntax!");
        }
        free(segment);
        // Advance p past delimiter just parsed
        if (delim == ';') { p = end + 1; }
        else if (delim == '&') { p = end + 1; }
        else if (delim == 'A') { p = end + 2; }
        else { p = end; }

        // Short-circuit for && when last_status != 0: skip to next ';' or end
        if (delim == 'A' && last_status != 0) {
            while (*p && *p != ';') {
                // also skip over single '&' and '&&' segments entirely
                if (*p == '&' && p[1] == '&') { p += 2; continue; }
                if (*p == '&' || *p == ';') { break; }
                p++;
            }
            if (*p == ';') p++; // consume ';' if encountered
        }
    }
    return last_status;
}
