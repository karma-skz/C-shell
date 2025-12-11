#include "parser.h"
#include "parser.h"
// Parser module
// -------------
// This validates user input against a very small shell grammar. It does not
// build an AST; it only checks whether the structure of the command is valid.
// Keeping the parser separate from the executor lets us give quick feedback
// ("Invalid Syntax!") before trying to run anything.
//
// Supported grammar (whitespace is allowed around tokens):
//   shell_cmd  ->  cmd_group (( '&&' | '&' | ';') cmd_group)* ('&' | ';')?
//   cmd_group  ->  atomic ( '|' atomic )*
//   atomic     ->  name ( name | input | output )*
//   input      ->  '<' WS* name
//   output     ->  ('>' | '>>') WS* name
//   name       ->  [^|&><;\s]+  (we stop at whitespace or special characters)
//
// Notes:
// - This is a hand-written, single-pass recursive-descent parser.
// - We do not handle quotes or escapes to keep it beginner-friendly.
// - The executor performs the real splitting later; this step just validates.
#include <ctype.h>
#include <string.h>

// grammar
// shell_cmd  ->  cmd_group (( '&&' | '&' | ';') cmd_group)* ('&' | ';')?
// cmd_group  ->  atomic ( '|' atomic )*
// atomic     ->  name ( name | input | output )*
// input      ->  '<' WS* name
// output     ->  ('>' | '>>') WS* name
// name       ->  [^|&><;]+ (trim trailing WS outside)

typedef struct {
    const char *s; // original string
    size_t i;      // current index
} Parser;

static void skip_ws(Parser *p) {
    while (p->s[p->i] && (p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n' || p->s[p->i] == '\r'))
        p->i++;
}

// name -> one or more chars not in "|&><;". We do not trim here; caller should skip_ws around tokens.
static int parse_name(Parser *p) {
    size_t start = p->i;
    while (p->s[p->i]) {
        char c = p->s[p->i];
        if (c == '|' || c == '&' || c == '>' || c == '<' || c == ';') break;
        if (c == '\n' || c == '\r') break; // treat newline as separator/end
        // For simplicity we treat whitespace as token separators; this avoids
        // ambiguities and keeps the beginner grammar easy to reason about.
        if (c == ' ' || c == '\t') break;
        p->i++;
    }
    return p->i > start; // at least one char
}

// input -> '<' WS* name
static int parse_input(Parser *p) {
    size_t save = p->i;
    if (p->s[p->i] != '<') return 0;
    p->i++; // consume '<'
    skip_ws(p);
    if (!parse_name(p)) { p->i = save; return 0; }
    return 1;
}

// output -> ('>' | '>>') WS* name
static int parse_output(Parser *p) {
    size_t save = p->i;
    if (p->s[p->i] != '>') return 0;
    p->i++; // consume '>'
    if (p->s[p->i] == '>') {
        p->i++; // '>>'
    }
    skip_ws(p);
    if (!parse_name(p)) { p->i = save; return 0; }
    return 1;
}

// atomic -> name ( name | input | output )*
static int parse_atomic(Parser *p) {
    skip_ws(p);
    if (!parse_name(p)) return 0; // must start with a name
    for (;;) {
        size_t save = p->i;
        skip_ws(p);
        // try input/output first (they start with < or >)
        if (parse_input(p)) continue;
        if (parse_output(p)) continue;
        // else try another name (argument)
        if (parse_name(p)) continue;
        // nothing more for atomic
        p->i = save; // restore to position before WS skip for clean caller behavior
        return 1;
    }
}

// cmd_group -> atomic ( '|' atomic )*
static int parse_cmd_group(Parser *p) {
    if (!parse_atomic(p)) return 0;
    for (;;) {
        size_t save = p->i;
        skip_ws(p);
        if (p->s[p->i] == '|') {
            p->i++; // consume '|'
            if (!parse_atomic(p)) { p->i = save; return 0; } // pipe must be followed by atomic
            continue;
        }
        p->i = save;
        return 1;
    }
}

// shell_cmd  ->  cmd_group (( '&&' | '&' | ';') cmd_group)* ('&' | ';')?
static int parse_shell_cmd(Parser *p) {
    if (!parse_cmd_group(p)) return 0;
    for (;;) {
        size_t save = p->i;
        skip_ws(p);
        if (p->s[p->i] == ';') {
            // Lookahead to allow a trailing ';' with no following cmd_group
            size_t j = p->i + 1;
            while (p->s[j] == ' ' || p->s[j] == '\t' || p->s[j] == '\n' || p->s[j] == '\r') j++;
            if (p->s[j] == '\0') {
                // trailing ';' — leave for optional trailer consumption
                p->i = save;
                break;
            }
            p->i++;
            if (!parse_cmd_group(p)) { p->i = save; return 0; }
            continue;
        } else if (p->s[p->i] == '&') {
            // Check for '&&' first (conditional AND)
            if (p->s[p->i+1] == '&') {
                size_t j = p->i + 2; // skip '&&'
                while (p->s[j] == ' ' || p->s[j] == '\t' || p->s[j] == '\n' || p->s[j] == '\r') j++;
                if (p->s[j] == '\0') { p->i = save; return 0; } // '&&' must be followed by a command
                p->i += 2;
                if (!parse_cmd_group(p)) { p->i = save; return 0; }
                continue;
            } else {
                // Single '&' behaves like ';' but marks background; must be followed by a command unless trailing
                size_t j = p->i + 1;
                while (p->s[j] == ' ' || p->s[j] == '\t' || p->s[j] == '\n' || p->s[j] == '\r') j++;
                if (p->s[j] == '\0') {
                    // trailing &, let the optional handler consume it
                    p->i = save;
                    break;
                }
                p->i++;
                if (!parse_cmd_group(p)) { p->i = save; return 0; }
                continue;
            }
        }
        p->i = save;
        break;
    }

    // optional trailing & or ;
    skip_ws(p);
    if (p->s[p->i] == '&' || p->s[p->i] == ';') {
        p->i++;
    }
    skip_ws(p);
    return 1;
}

int parse_command(const char *s) {
    if (!s) return 0;
    Parser p = { .s = s, .i = 0 };
    int ok = parse_shell_cmd(&p);
    if (!ok) return 0;
    // after parse, ensure no trailing non-ws garbage like stray characters
    skip_ws(&p);
    // Accept if we are at end or at a single newline terminator.
    return s[p.i] == '\0' || s[p.i] == '\n' || s[p.i] == '\r';
}
