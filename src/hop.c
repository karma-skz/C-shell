// hop.c: directory navigation builtins (hop and cd)
// -----------------------------------------------
// This module implements two builtins:
//   - hop: a playful version of cd that accepts multiple arguments and moves
//          through them one by one (like: hop .. - /tmp)
//   - cd : classic cd with the usual constraints (at most one argument)
//
// Shared helper rules:
//   ~  -> shell home (the directory where the shell started)
//   .  -> no-op
//   .. -> parent directory
//   -  -> previous directory (tracked in this module)
//   name/path -> chdir() there
// On error we print "No such directory!" as required by the assignment.
//
// Why keep prev directory here? 'reveal -' also needs it, so we expose
// two tiny helper functions for other modules to query the previous CWD.

#include "hop.h"
#include "prompt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/limits.h> // for PATH_MAX

// Track previous working directory across calls
static char prev_cwd[PATH_MAX] = "";
static int prev_cwd_set = 0; // becomes 1 after first non-"-" hop that changes dir

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
}

static int is_word_char(char c) {
    return c != '\0' && c != ' ' && c != '\t' && c != '\n' && c != '\r';
}

static char *next_token(const char **p) {
    skip_ws(p);
    if (**p == '\0') return NULL;
    const char *start = *p;
    while (is_word_char(**p)) (*p)++;
    size_t len = (size_t)(*p - start);
    char *tok = (char *)malloc(len + 1);
    if (!tok) return NULL;
    memcpy(tok, start, len);
    tok[len] = '\0';
    return tok;
}

static int change_dir_to(const char *target, int record_prev) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        // If we can't getcwd, attempt chdir anyway; if chdir fails, report.
        cwd[0] = '\0';
    }
    if (chdir(target) != 0) {
        puts("No such directory!");
        return 0;
    }
    if (record_prev && cwd[0] != '\0') {
        // Record prev only after a successful hop to a real target that's not '-'
        strncpy(prev_cwd, cwd, sizeof(prev_cwd));
        prev_cwd[sizeof(prev_cwd)-1] = '\0';
        prev_cwd_set = 1;
    }
    return 1;
}

static void hop_one(const char *arg) {
    if (arg == NULL || strcmp(arg, "~") == 0) {
        const char *home = prompt_home();
        if (home && *home) (void)change_dir_to(home, 1);
        return;
    }
    if (strcmp(arg, ".") == 0) {
        // do nothing
        return;
    }
    if (strcmp(arg, "..") == 0) {
        (void)change_dir_to("..", 1);
        return;
    }
    if (strcmp(arg, "-") == 0) {
        if (prev_cwd_set && prev_cwd[0] != '\0') {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd))) {
                // Attempt swap: go to prev, then set prev to old cwd if chdir succeeded
                if (chdir(prev_cwd) == 0) {
                    strncpy(prev_cwd, cwd, sizeof(prev_cwd));
                    prev_cwd[sizeof(prev_cwd)-1] = '\0';
                    // prev_cwd_set remains true
                }
            }
        }
        return;
    }
    // name: relative or absolute path
    (void)change_dir_to(arg, 1);
}

bool try_handle_hop(const char *input) {
    if (!input) return false;
    const char *p = input;
    skip_ws(&p);
    const char *kw = "hop";
    size_t kwlen = 3;
    if (strncmp(p, kw, kwlen) != 0) return false;
    p += kwlen;
    // Ensure hop is a whole word (next char must be WS or end)
    if (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') return false;

    // Parse zero or more args and execute sequentially
    char *tok = NULL;
    while ((tok = next_token(&p)) != NULL) {
        hop_one(tok);
        free(tok);
    }
    // If no arguments at all (just hop), behave like '~'
    // Detect no-arg by rewinding check: find first token after 'hop'
    // Simplify: if we consumed only whitespace after 'hop', tok would have been NULL immediately.
    // We'll re-check by scanning the input after 'hop' for non-ws.
    const char *q = input;
    skip_ws(&q);
    q += kwlen;
    skip_ws(&q);
    if (*q == '\0') {
        const char *home = prompt_home();
        if (home && *home) (void)change_dir_to(home, 1);
    }
    return true;
}

int hop_prev_cwd_available(void) {
    return prev_cwd_set && prev_cwd[0] != '\0';
}

const char* hop_get_prev_cwd(void) {
    return hop_prev_cwd_available() ? prev_cwd : NULL;
}

// Basic 'cd' built-in: mirrors hop behavior but with typical cd constraints.
// - No args or '~' -> home
// - '.' -> no-op
// - '..' -> parent
// - '-' -> previous directory (error if none)
// - name -> path
// - More than one positional arg -> print error and ignore

bool try_handle_cd(const char *input){
    if(!input) return false;
    const char *p=input; skip_ws(&p);
    if(strncmp(p, "cd", 2)!=0) return false;
    p+=2;
    if(*p!='\0' && *p!=' ' && *p!='\t' && *p!='\n' && *p!='\r') return false; // not a standalone 'cd'
    // Collect at most one argument
    char *arg=NULL; skip_ws(&p);
    const char *arg_start=p;
    while(*p && *p!=' '&&*p!='\t'&&*p!='\n'&&*p!='\r') p++;
    if(p>arg_start){ arg = strndup(arg_start, (size_t)(p-arg_start)); }
    // Ensure no more args
    const char *q = p; skip_ws(&q);
    if(*q!='\0'){ puts("cd: too many arguments"); free(arg); return true; }

    // Map to hop behavior
    if(arg==NULL || strcmp(arg, "~")==0){ const char *home=prompt_home(); if(home&&*home) (void)change_dir_to(home, 1); }
    else if(strcmp(arg, ".")==0){ /* no-op */ }
    else if(strcmp(arg, "..")==0){ (void)change_dir_to("..", 1); }
    else if(strcmp(arg, "-")==0){ if(prev_cwd_set && prev_cwd[0]){ char cwd[PATH_MAX]; if(getcwd(cwd,sizeof(cwd))){ if(chdir(prev_cwd)==0){ strncpy(prev_cwd,cwd,sizeof(prev_cwd)); prev_cwd[sizeof(prev_cwd)-1]='\0'; } } } else { puts("No such directory!"); } }
    else { (void)change_dir_to(arg, 1); }
    free(arg);
    return true;
}

// argv-based versions so the executor can run hop/cd inside a fork with redirection.
int run_hop_argv(int argc, char **argv) {
    if (argc <= 0) return 1;
    // argv[0] == "hop"
    if (argc == 1) {
        const char *home = prompt_home();
        if (home && *home) change_dir_to(home, 1);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        hop_one(argv[i]);
    }
    return 0;
}

int run_cd_argv(int argc, char **argv) {
    if (argc <= 0) return 1;
    // Behavior: only zero or one arg allowed.
    if (argc > 2) {
        puts("cd: too many arguments");
        return 1;
    }
    if (argc == 1 || strcmp(argv[1], "~") == 0) {
        const char *home = prompt_home(); if (home && *home) change_dir_to(home, 1); return 0;
    }
    const char *arg = argv[1];
    if (strcmp(arg, ".") == 0) return 0;
    if (strcmp(arg, "..") == 0) { change_dir_to("..", 1); return 0; }
    if (strcmp(arg, "-") == 0) {
        if (prev_cwd_set && prev_cwd[0]) { char cwd[PATH_MAX]; if (getcwd(cwd,sizeof(cwd))) { if (chdir(prev_cwd)==0) { strncpy(prev_cwd,cwd,sizeof(prev_cwd)); prev_cwd[sizeof(prev_cwd)-1]='\0'; } } }
        else { puts("No such directory!"); return 1; }
        return 0;
    }
    change_dir_to(arg, 1);
    return 0;
}
