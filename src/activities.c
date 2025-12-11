// activities.c: list running/stopped activities
// ---------------------------------------------
// Implements the 'activities' builtin, which prints processes tracked by the
// job system (both running and stopped pipeline stages).
// The executor provides an iterator (executor_for_each_activity) that we use
// to collect snapshot information and then print a sorted list.

#include "activities.h"
#include "executor.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct { pid_t pid; char *name; int stopped; } Act;

static int collect_cb(pid_t pid, const char *name, int stopped, void *ud){
    Act **arrp = (Act**)ud;
    Act *arr = *arrp;
    // arr staged with capacity MAX? We'll realloc dynamically.
    static int cap = 0; static int len = 0;
    if(arr == NULL){
        cap = 16; len = 0; arr = malloc(sizeof(Act)*cap); *arrp = arr;
    }
    if(len >= cap){ cap*=2; arr = realloc(arr, sizeof(Act)*cap); *arrp = arr; }
    arr[len].pid = pid;
    arr[len].name = strdup(name ? name : "?");
    arr[len].stopped = stopped;
    len++;
    return 0;
}

int run_activities_argv(int argc, char **argv){
    (void)argc; (void)argv;
    Act *acts=NULL;
    int total = executor_for_each_activity(collect_cb, &acts);
    if(total <= 0){
        free(acts); return 0; // nothing to print
    }
    // Determine actual length stored in static collector state (hacky). We'll pass total.
    // Sort
    for(int i=0;i<total;i++){
        for(int j=i+1;j<total;j++){
            int cmp = strcmp(acts[i].name, acts[j].name);
            if(cmp>0 || (cmp==0 && acts[i].pid > acts[j].pid)){
                Act tmp = acts[i]; acts[i]=acts[j]; acts[j]=tmp;
            }
        }
    }
    for(int i=0;i<total;i++){
        printf("[%d] : %s - %s\n", acts[i].pid, acts[i].name, acts[i].stopped?"Stopped":"Running");
        free(acts[i].name);
    }
    free(acts);
    return 0;
}
