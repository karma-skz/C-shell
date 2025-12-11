// reveal.c: ls-like builtin
// -------------------------
// Implements a minimal directory listing command named 'reveal'.
// Usage:
//   reveal [-a] [-l] [path]
// Options:
//   -a : include hidden entries (names starting with '.')
//   -l : print one per line (otherwise print space-separated on one line)
// Path rules mirror hop/cd: ~ . .. - and normal paths.
//
// Design choices for beginners:
// - We collect entries then qsort() them for stable output.
// - Vector (Vec) is a tiny growable array to avoid fixed limits.
// - We intentionally do not show metadata like permissions to keep it short.

#include "reveal.h"
#include "prompt.h"
#include "hop.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} Vec;

static void vec_init(Vec *v) { v->items = NULL; v->len = 0; v->cap = 0; }
static void vec_free(Vec *v) {
    if (!v) return;
    for (size_t i = 0; i < v->len; i++) free(v->items[i]);
    free(v->items);
    v->items = NULL; v->len = v->cap = 0;
}
static int vec_push(Vec *v, const char *s) {
    if (v->len == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 16;
        char **ni = realloc(v->items, ncap * sizeof(char*));
        if (!ni) return 0;
        v->items = ni; v->cap = ncap;
    }
    v->items[v->len] = strdup(s);
    if (!v->items[v->len]) return 0;
    v->len++;
    return 1;
}

static void skip_ws(const char **p) { while (**p==' '||**p=='\t'||**p=='\n'||**p=='\r') (*p)++; }
static int is_word_char(char c){ return c!='\0' && c!=' ' && c!='\t' && c!='\n' && c!='\r'; }

static char *next_token(const char **p) {
    skip_ws(p);
    if (**p == '\0') return NULL;
    const char *start = *p;
    while (is_word_char(**p)) (*p)++;
    size_t len = (size_t)(*p - start);
    char *tok = malloc(len + 1);
    if (!tok) return NULL;
    memcpy(tok, start, len); tok[len] = '\0';
    return tok;
}

static int cmp_ascii(const void *a, const void *b) {
    const char * const *sa = a;
    const char * const *sb = b;
    // Lexicographic by ASCII values
    return strcmp(*sa, *sb);
}

// Expose prev directory state from hop for 'reveal -' requirement
extern int hop_prev_cwd_available(void);
extern const char* hop_get_prev_cwd(void);

static int list_dir(const char *path, int show_all, int line_by_line) {
    DIR *d = opendir(path);
    if (!d) {
        puts("No such directory!");
        return 0;
    }
    Vec v; vec_init(&v);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (!show_all && name[0] == '.') continue; // skip hidden unless -a
        if (!vec_push(&v, name)) { vec_free(&v); closedir(d); return 0; }
    }
    closedir(d);
    qsort(v.items, v.len, sizeof(char*), cmp_ascii);
    if (line_by_line) {
        for (size_t i = 0; i < v.len; i++) puts(v.items[i]);
    } else {
        // Simple ls-like: space-separated on one line
        for (size_t i = 0; i < v.len; i++) {
            fputs(v.items[i], stdout);
            if (i + 1 < v.len) fputc(' ', stdout);
        }
        if (v.len > 0) fputc('\n', stdout);
    }
    vec_free(&v);
    return 1;
}

bool try_handle_reveal(const char *input) {
    if (!input) return false;
    const char *p = input; skip_ws(&p);
    const char *kw = "reveal"; size_t kwlen = 6;
    if (strncmp(p, kw, kwlen) != 0) return false;
    p += kwlen;
    if (*p != '\0' && *p!=' ' && *p!='\t' && *p!='\n' && *p!='\r') return false;

    int show_all = 0;
    int line_by_line = 0;
    char *tok;
    Vec positional; vec_init(&positional);

    // Parse flags and collect positional args
    while ((tok = next_token(&p)) != NULL) {
        if (tok[0] == '-' && tok[1] != '\0') {
            for (size_t i = 1; tok[i] != '\0'; i++) {
                if (tok[i] == 'a') show_all = 1;
                else if (tok[i] == 'l') line_by_line = 1;
                else {
                    // Unknown flag -> treat like invalid syntax for reveal
                    puts("reveal: Invalid Syntax!");
                    free(tok);
                    for (size_t j=0;j<positional.len;j++) free(positional.items[j]);
                    free(positional.items);
                    return true;
                }
            }
            free(tok);
            continue;
        }
        // positional argument
        if (!vec_push(&positional, tok)) { free(tok); return true; }
        free(tok);
    }

    if (positional.len > 1) {
        puts("reveal: Invalid Syntax!");
        for (size_t j=0;j<positional.len;j++) free(positional.items[j]);
        free(positional.items);
        return true;
    }

    // Determine target directory similar to hop
    const char *target = NULL;
    char *resolved = NULL;
    if (positional.len == 0) {
        // No target: list current working directory
        target = ".";
    } else {
        const char *arg = positional.items[0];
        if (strcmp(arg, "~") == 0) {
            target = prompt_home();
        } else if (strcmp(arg, ".") == 0) {
            target = ".";
        } else if (strcmp(arg, "..") == 0) {
            target = "..";
        } else if (strcmp(arg, "-") == 0) {
            if (!hop_prev_cwd_available()) {
                puts("No such directory!");
                for (size_t j=0;j<positional.len;j++) free(positional.items[j]);
                free(positional.items);
                return true;
            }
            target = hop_get_prev_cwd();
        } else {
            target = arg;
        }
    }

    if (!target || target[0] == '\0') {
        puts("No such directory!");
        for (size_t j=0;j<positional.len;j++) free(positional.items[j]);
        free(positional.items);
        free(resolved);
        return true;
    }

    // Attempt to open directory and list
    (void)list_dir(target, show_all, line_by_line);

    for (size_t j=0;j<positional.len;j++) free(positional.items[j]);
    free(positional.items);
    free(resolved);
    return true;
}

// Simplified argv-based version: flags can be combined (-al) and at most one positional path.
int run_reveal_argv(int argc, char **argv) {
    if (argc <= 0) return 1;
    int show_all = 0, line_by_line = 0; const char *target = ".";
    int positional_count = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1] != '\0') {
            for (int j = 1; a[j]; j++) {
                if (a[j] == 'a') show_all = 1;
                else if (a[j] == 'l') line_by_line = 1;
                else { puts("reveal: Invalid Syntax!"); return 1; }
            }
            continue;
        }
        positional_count++;
        if (positional_count > 1) { puts("reveal: Invalid Syntax!"); return 1; }
        if (strcmp(a, "~") == 0) target = prompt_home();
        else if (strcmp(a, ".") == 0) target = ".";
        else if (strcmp(a, "..") == 0) target = "..";
        else if (strcmp(a, "-") == 0) {
            if (!hop_prev_cwd_available()) { puts("No such directory!"); return 1; }
            target = hop_get_prev_cwd();
        } else target = a;
    }
    if (!target) { puts("No such directory!"); return 1; }
    list_dir(target, show_all, line_by_line);
    return 0;
}
