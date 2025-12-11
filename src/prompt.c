// Prompt module
// -------------
// Builds and prints the prompt of the form "<user@host:path> ".
// The shell's "home" is defined as the directory where the shell started
// (not necessarily the OS account's HOME). When the current directory is a
// descendant of this home, we replace the home prefix with '~'.
// Example: if the shell started in "/code", then "/code" prints as "~",
// and "/code/src" prints as "~/src".

#include "prompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>

static char hostname[256];
static const char *username;
static char *shell_home = NULL; // shell's "home" = directory where shell started

static void print_prompt_from_cwd(const char *cwd){
    const char *path_to_show=cwd;

    if(shell_home && cwd){
        size_t home_len = strlen(shell_home);
         // If the shell started in "/", treat everything as a descendant.
        if (shell_home[0]=='/' && shell_home[1]=='\0' && cwd[0]=='/'){
            if (cwd[1]=='\0') {
                printf("<%s@%s:~>", username, hostname);
                return;
            } 
            else{
                printf("<%s@%s:~/%s>", username, hostname, cwd + 1);
                return;
            }
        }
        if(strncmp(cwd, shell_home, home_len)==0){
            // exact home: "/path/to/home" -> "~"
            if (cwd[home_len]=='\0'){
                path_to_show="~";
            }
            // descendant: "/path/to/home/xyz" -> "~/xyz"
            else if(cwd[home_len]=='/'){
                printf("<%s@%s:~%s>", username, hostname, cwd+home_len);
                return;
            }
        }
    }

    printf("<%s@%s:%s>", username, hostname, path_to_show? path_to_show:"?");
}

void prompt_init(void){
    // hostname
    // try to get the hostname. if it fails, show an error and use ? instead. 
    // if it succeeds, make sure the string ends with \0 just in case.
    if(gethostname(hostname, sizeof(hostname) - 1) != 0) strcpy(hostname, "host");
    else hostname[sizeof(hostname) - 1] = '\0';

    // username (fallback if getlogin() fails)
    username = getlogin();
    if(!username){
        struct passwd *pw = getpwuid(getuid());
        username=(pw && pw->pw_name)? pw->pw_name :"?";
    }

    shell_home=getcwd(NULL, 0);
    if(!shell_home){
        shell_home=strdup("?");
        // continue. we'll still try to print prompts even if home unknown
    }
}

void prompt_print(void){
    char *cwd=getcwd(NULL, 0);
    if (!cwd){
        print_prompt_from_cwd(NULL);
        putchar(' ');
        fflush(stdout);
        return;
    }
    print_prompt_from_cwd(cwd);
    free(cwd);
    putchar(' ');
    fflush(stdout);
}

void prompt_cleanup(void){
    fflush(stdout);
    free(shell_home);
    shell_home = NULL;
}

const char* prompt_home(void){
    return shell_home;
}