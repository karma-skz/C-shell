// ping.c: send a signal to a process
// ----------------------------------
// Implements: ping <pid> <signal_number>
// Examples:
//   ping 1234 9   -> send SIGKILL to process 1234
//   ping 1234 19  -> send SIGSTOP (19 on many systems)
// Notes:
// - We parse integers safely using strtol.
// - If the process doesn't exist, we print "No such process found".
// - Signal number is modulo 32 like many student shells (keeps values small).

#include "ping.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdbool.h>

static int parse_int(const char *s, long *out){
    if(!s||!*s) return 0;
    char *end=NULL;
    errno=0;
    long v=strtol(s,&end,10);
    if(errno!=0) return 0;
    if(end==s || *end!='\0') return 0;
    *out=v; return 1;
}

int run_ping_argv(int argc, char **argv){
    if(argc!=3){
        puts("ping: Invalid Syntax!");
        return 1;
    }
    long pid_l=0, sig_l=0;
    if(!parse_int(argv[1], &pid_l) || pid_l<=0){
        puts("No such process found");
        return 1;
    }
    if(!parse_int(argv[2], &sig_l)){
        puts("ping: Invalid Syntax!");
        return 1;
    }
    int actual = (int)(sig_l % 32);
    if(actual==0) actual = 0; // signal 0 just error-checks permission/existence
    int rc = kill((pid_t)pid_l, actual);
    if(rc!=0){
        if(errno==ESRCH){
            puts("No such process found");
        } else {
            perror("kill");
        }
        return 1;
    }
    printf("Sent signal %ld to process with pid %ld\n", sig_l, pid_l);
    return 0;
}

bool try_handle_ping(const char *line){
    if(!line) return false;
    const char *p=line; while(*p==' '||*p=='\t') p++;
    if(strncmp(p,"ping",4)!=0) return false;
    // We rely on executor path; just return false to allow unified parsing.
    return false;
}
