// jobs.c: simple job control
// --------------------------
// This file maintains a tiny in-memory table of background jobs and the current
// foreground pipeline. A job is a pipeline (one or more processes connected by
// pipes) that share the same process group ID (pgid). We track per-stage PIDs
// and whether each stage is finished or stopped.
//
// Key ideas to learn:
// - Foreground vs background: only the foreground pgid is given the terminal by
//   tcsetpgrp(). Background jobs are detached from stdin by the executor.
// - waitpid(WNOHANG|WUNTRACED|WCONTINUED) lets us poll jobs without blocking and
//   detect state changes (stopped/continued/finished).
// - We print completion messages when all stages in a background job finish.
// - Builtins 'fg' and 'bg' use this table to resume jobs or bring them back.
//
// This is not a production-grade job control implementation, but it's small and
// clear, which is perfect for learning.
//
// jobs.c - job control (background table, fg/bg builtins, activities enumeration)
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <time.h>

#define MAX_CMDS 16
#define MAX_BG_JOBS 64

typedef struct {
    int job_num;
    int npids;
    pid_t pids[MAX_CMDS];
    int finished[MAX_CMDS];
    int stopped[MAX_CMDS];
    char *cmd_name;
    char *stage_names[MAX_CMDS];
    int last_status;
} BgJob;

static BgJob bg_jobs[MAX_BG_JOBS];
static int bg_job_count = 0;
static int next_job_number = 1;

// Foreground tracking
static pid_t fg_pgid = -1;
static pid_t fg_pids[MAX_CMDS];
static int fg_count = 0;
static char fg_name[128];

void jobs_set_foreground(pid_t pgid, const pid_t *pids, int count, const char *name){
    fg_pgid = pgid; fg_count = count>MAX_CMDS?MAX_CMDS:count;
    for(int i=0;i<fg_count;i++) fg_pids[i]=pids[i];
    if(name){ strncpy(fg_name,name,sizeof(fg_name)-1); fg_name[sizeof(fg_name)-1]='\0'; } else fg_name[0]='\0';
}
void jobs_clear_foreground(void){ fg_pgid=-1; fg_count=0; fg_name[0]='\0'; }
int jobs_get_foreground(pid_t *pgid_out, pid_t *pids_out, int max, char *name_buf, size_t name_sz){
    if (fg_pgid == -1) return 0;
    if (pgid_out) *pgid_out = fg_pgid;
    int n = (fg_count < max ? fg_count : max);
    if (pids_out) {
        for (int i=0;i<n;i++) pids_out[i] = fg_pids[i];
    }
    if (name_buf && name_sz) {
        strncpy(name_buf, fg_name, name_sz-1);
        name_buf[name_sz-1] = '\0';
    }
    return fg_count;
}

int jobs_move_foreground_to_background_stopped(void){
    if (fg_pgid==-1 || fg_count==0) return -1;
    if (bg_job_count>=MAX_BG_JOBS) return -1;
    BgJob *job=&bg_jobs[bg_job_count];
    memset(job,0,sizeof(*job));
    job->job_num=next_job_number++;
    job->npids=fg_count;
    job->cmd_name=strdup(fg_name[0]?fg_name:"?");
    for(int i=0;i<fg_count;i++){
        job->pids[i]=fg_pids[i];
        job->stage_names[i]=strdup(fg_name[0]?fg_name:"?");
        job->stopped[i]=1;
    }
    bg_job_count++;
    int num=job->job_num;
    jobs_clear_foreground();
    return num;
}

int jobs_add_background(const pid_t *pids, int count, const char *const *stage_names, pid_t *last_pid_out){
    if(count<=0) return -1;
    if(bg_job_count>=MAX_BG_JOBS) return -1;
    BgJob *job=&bg_jobs[bg_job_count]; memset(job,0,sizeof(*job));
    job->job_num = next_job_number++;
    job->npids = count;
    job->cmd_name = strdup(stage_names && stage_names[0]? stage_names[0] : "?");
    for(int i=0;i<count;i++){
        job->pids[i]=pids[i];
        job->stage_names[i]=strdup(stage_names && stage_names[i]?stage_names[i]:job->cmd_name);
        job->stopped[i]=0;
    }
    bg_job_count++;
    if(last_pid_out) *last_pid_out = pids[count-1];
    return job->job_num;
}

void jobs_poll(void){
    for(int i=0;i<bg_job_count;){
        BgJob *job=&bg_jobs[i];
        int all_done=1;
        for(int j=0;j<job->npids;j++){
            if(job->finished[j]) continue;
            int st=0; pid_t w=waitpid(job->pids[j], &st, WNOHANG|WUNTRACED
#ifdef WCONTINUED
                                        | WCONTINUED
#endif
                                        );
            if(w==0){ all_done=0; continue; }
            if(w==-1) continue;
            if(WIFSTOPPED(st)){ job->stopped[j]=1; all_done=0; continue; }
            if(WIFCONTINUED(st)){ job->stopped[j]=0; all_done=0; continue; }
            job->finished[j]=1; job->stopped[j]=0;
            if(j==job->npids-1){ job->last_status = (WIFEXITED(st) && WEXITSTATUS(st)==0)?0:1; }
        }
        if(all_done){
            if(job->last_status==0)
                printf("%s with pid %d exited normally\n", job->cmd_name, job->pids[job->npids-1]);
            else
                printf("%s with pid %d exited abnormally\n", job->cmd_name, job->pids[job->npids-1]);
            fflush(stdout);
            free(job->cmd_name);
            for(int j=0;j<job->npids;j++) free(job->stage_names[j]);
            if(i<bg_job_count-1) memmove(&bg_jobs[i], &bg_jobs[i+1], (bg_job_count-i-1)*sizeof(BgJob));
            bg_job_count--;
            continue;
        }
        i++;
    }
}

int jobs_for_each_activity(int (*cb)(pid_t pid,const char*name,int stopped,void*ud), void *ud){
    if(!cb) return 0;
    int count=0;
    for(int i=0;i<bg_job_count;i++){
        BgJob *job=&bg_jobs[i];
        for(int j=0;j<job->npids;j++){
            if(job->finished[j]) continue;
            const char *nm = job->stage_names[j]?job->stage_names[j]:job->cmd_name;
            cb(job->pids[j], nm, job->stopped[j], ud);
            count++;
        }
    }
    return count;
}

// helpers
static int find_job_index(int jobnum){ for(int i=0;i<bg_job_count;i++) if(bg_jobs[i].job_num==jobnum) return i; return -1; }
static int most_recent_job_index(void){ return bg_job_count?bg_job_count-1:-1; }

int jobs_cmd_bg(int jobnum){ int idx= jobnum?find_job_index(jobnum):most_recent_job_index(); if(idx<0){ puts("No such job"); return 1;} BgJob*job=&bg_jobs[idx]; int any_stopped=0; for(int i=0;i<job->npids;i++) if(!job->finished[i] && job->stopped[i]) any_stopped=1; if(!any_stopped){ puts("Job already running"); return 1;} pid_t pgid=job->pids[0]; if(pgid>0) kill(-pgid,SIGCONT); for(int i=0;i<job->npids;i++) job->stopped[i]=0; printf("[%d] %s &\n", job->job_num, job->cmd_name); fflush(stdout); return 0; }

int jobs_cmd_fg(int jobnum){ int idx= jobnum?find_job_index(jobnum):most_recent_job_index(); if(idx<0){ puts("No such job"); return 1;} BgJob*job=&bg_jobs[idx]; pid_t pgid=job->pids[0]; if(pgid<=0){ puts("No such job"); return 1;} printf("%s\n", job->cmd_name); fflush(stdout); tcsetpgrp(STDIN_FILENO, pgid); int need_cont=0; for(int i=0;i<job->npids;i++) if(job->stopped[i]) { need_cont=1; break; } if(need_cont) kill(-pgid,SIGCONT); int stopped=0; int status_code=0; for(;;){ int all_done=1; stopped=0; for(int i=0;i<job->npids;i++){ if(job->finished[i]) continue; int st; pid_t w=waitpid(job->pids[i], &st, WUNTRACED
#ifdef WCONTINUED
            | WCONTINUED
#endif
            | WNOHANG); if(w==0){ all_done=0; continue;} if(w<0) continue; if(WIFSTOPPED(st)){ job->stopped[i]=1; all_done=0; stopped=1; } else if(WIFCONTINUED(st)){ job->stopped[i]=0; all_done=0; } else { job->finished[i]=1; job->stopped[i]=0; if(i==job->npids-1){ if(WIFEXITED(st)&&WEXITSTATUS(st)==0) status_code=0; else status_code=1; } } }
        if(stopped){ tcsetpgrp(STDIN_FILENO, getpgrp()); printf("[%d] Stopped %s\n", job->job_num, job->cmd_name); fflush(stdout); return 148; }
        if(all_done){ free(job->cmd_name); for(int j=0;j<job->npids;j++) free(job->stage_names[j]); if(idx<bg_job_count-1) memmove(&bg_jobs[idx],&bg_jobs[idx+1],(bg_job_count-idx-1)*sizeof(BgJob)); bg_job_count--; break; }
        struct timespec ts={0,30*1000*1000}; nanosleep(&ts,NULL);
    }
    tcsetpgrp(STDIN_FILENO, getpgrp()); return status_code; }
