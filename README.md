# C Shell \[Total: 645\]

## General Requirements

- The project must be broken down into multiple .c and .h files based on functionality. Monolithic code in a single file will be heavily penalized.
- You may only use the C POSIX library headers and functions. The documentation for these are provided here - <https://pubs.opengroup.org/onlinepubs/9699919799/idx/head.html>.
- Use the below `gcc` feature flags while compiling to ensure POSIX compliance.

  ```
  gcc -std=c99 \
    -D_POSIX_C_SOURCE=200809L \
    -D_XOPEN_SOURCE=700 \
    -Wall -Wextra -Werror \
    -Wno-unused-parameter \
    -fno-asm \
    your_file.c
  ```

- Your final code submission **must** be compilable using the command `make all` in the **shell directory** of the git repository. It **must** compile the shell to the file `shell.out`. If not done, this would cause automatic evaluation to fail, leading to zero marks. ~A test script will be provided soon~ (Done, see Moodle). This binary should be created in the **shell directory**, and not the project root.

## Part A: Shell Input \[Total: 65\]

This is the base for the rest of the assignment. Work hard on this!

**Banned Syscalls for Part A**: `exec*`, i.e. any of the syscalls whose names start with exec.

### A.1: The Shell Prompt \[10\]

Your shell should show a prompt so that the user knows that they can provide input to it. Successfully completing most of this requirement merely requires your shell to display the below prompt!

`<Username@SystemName:current_path>`

Find out what Username and SystemName are by investigating the bash (or zsh, etc.) prompt that you see in your computer's shell. Unix only, sorry non-WSL Windows users :(

#### Requirements

1. The shell should display the above prompt when it is not running a foreground process.
2. The directory in which the shell is started becomes the shell's home directory.
3. When the current working directory has the home directory as an ancestor (for example `/path/to/home/meow`), the absolute path to the home directory must be replaced by a tilde "~". So the example would be displayed as `~/meow`.
4. When the current working directory does not have the home directory as an ancestor (for example `/path/to/not_home/meow`) the absolute path should be displayed as is. So the example would be displayed as `/path/to/not_home/meow`.

#### Example

```
# I am currently running bash
[rudy@iiit ~/osnmp1/]$ make
[rudy@iiit ~/osnmp1/]$ ./shell.out
# I am now running my shell
<rudy@iiit:~>
```

### A.2: User Input \[5\]

A shell's job is to take input from a user, parse it, and then run any commands if necessary. For now, after taking input from a user, the shell doesn't have to do anything with the input! So successfully completing this requirement merely requires your shell to be able to take user input!

#### Requirements

1. The shell should allow a user to type input.
2. When the user presses the enter/return key, the shell should consume the input.
3. After consuming the input, the shell should once again display the shell prompt. The user should again be able to type input and submit it (by pressing the enter/return key).

#### Example

```
<rudy@iiit:~> Hi there guys!
<rudy@iiit:~> This shell is cool!
<rudy@iiit:~>
```

### A.3: Input Parsing \[50\]

After taking user input, the input must be parsed so that we can decide what the user wants the shell to do. You will be implementing a parser for the below Context Free Grammar

```
shell_cmd  ->  cmd_group ((& | ;) cmd_group)* &?
cmd_group ->  atomic (\| atomic)*
atomic -> name (name | input | output)*
input -> < name | <name
output -> > name | >name | >> name | >>name
name -> r"[^|&><;\s]+"
```

Below is another version of the **same grammar**, but provided in a pure regex format. This is to clarify that in the above grammar, **anywhere there is a space, you must handle an arbitrary amount of whitespace**. Note the usage of '*' and '+'.

```
shell_cmd  ->  r"(?P<cmd_group1>.+)(?P<WS1>\s*)((&|;)(?P<WS2>\s*)(?P<cmd_group2>.+))*(?P<WS3>\s*)&?"
cmd_group ->  r"(?P<atomic1>.+)(?P<WS1>\s*)(\|(?P<WS2>\s*)(?P<atomic2>.+))*"
atomic -> r"(?P<NAME1>[^|&><;\s]+)(?P<WS1>\s+)((?P<NAME2>[^|&><;\s]+)|(?P<input>.+)|(?P<output>.+))*"
input -> r"<(?P<WS>\s*)(?P<NAME>[^|&><;\s]+)"
output -> r"(>|>>)(?P<WS>\s*)(?P<NAME>[^|&><;\s]+)"
```

Instead of attempting to understand any of these regexes, I highly recommend copying the regex (the text between the double quotes) and pasting it into [Regex101](https://regex101.com/). Ensure that you select the flavor to be 'Python' and that you disable the 'global' and 'multiline' options which are auto enabled in the 'Regex Flags' menu. An explanation for the provided regex can be seen on the right hand side.

A simple introduction to Context Free Grammars and an example parser for a simpler grammar will be provided in the tutorial.

Parsers for such programs usually create something called an Abstract Syntax Tree. However, you are not required to parse the input into an AST. You can use any structure that you find the most convenient!

#### Requirements

1. The shell should verify whether or not an inputted command is valid or invalid using the rules of the grammar.
2. If a command is valid, do nothing (for now)! For example, `cat meow.txt | meow; meow > meow.txt &` is a valid command.
3. If a command is invalid, print "Invalid Syntax!". For example, `cat meow.txt | ; meow` is an invalid command.
4. Your parser should ignore whitespace characters (space, tab (\t), new line (\n), and carriage return (\r)) in between valid tokens.

#### Example

```
# This is valid syntax
<rudy@iiit:~> Hi there guys!
# This isn't
<rudy@iiit:~> cat meow.txt | ; meow
Invalid Syntax!
<rudy@iiit:~>
```

## Part B: Shell Intrinsics \[Total: 70\]

These are commands that any shell worth its salt supports! Implementing them shouldn't be too difficult.

**Banned Syscalls for Part B**: `exec*`, i.e. any of the syscalls whose names start with exec.

### B.1: hop \[20\]

**Syntax**: `hop ((~ | . | .. | - | name)*)?`

**Purpose**: The hop command allows a user to change the shell's current working directory.

#### Requirements

Execute one of the following operations sequentially for each passed argument:

1. "~" or No Arguments: Change the CWD to the shell's home directory.
2. ".": Do nothing (i.e. stay in the same CWD)
3. "..": Change the CWD to the parent directory of the CWD, or do nothing if the CWD has no parent directory.
4. "-": Change the CWD to the previous CWD or do nothing if there was no previous CWD. So, after starting the shell, till the first hop command which was not a "-" was run, you must do nothing.
5. "name": Change the CWD to the specified relative or absolute path.
6. If the directory does not exist, output "No such directory!"

#### Example

```
<rudy@iiit:~/osnmp1> hop ~
<rudy@iiit:~> hop ..
<rudy@iiit:/home/> hop rudy/osnmp1 .. -
<rudy@iiit:~/osnmp1>
```

### B.2: reveal \[20\]

**Syntax**: `reveal (-(a | l)*)* (~ | . |.. | - | name)?`

**Purpose**: The reveal command allows a user to view the files and directories in a directory.

#### Requirements

Flags modify the default behavior of reveal.

1. "a": When this flag is set, reveal all files and directories, including hidden ones (files and directories starting with .). The default behavior is to not reveal hidden files and directories.
2. "l": When this flag is set, reveal files and directories in a line by line format, i.e. only one entry per line.
3. When both flags are set, all files and directories, including hidden ones, must be printed in the line by line format.
4. When neither flag is set, print files and directories in the format of `ls`.
5. The argument passed invokes identical behavior to hop, except that here we are listing directory contents instead of changing the CWD.
6. Ensure that the files are always listed in lexicographic order.
   Note that you are **not** required to implement the format of `ls -l`. (In fact if you do you may lose marks due to the automated evaluation!)
   Further note that here you must use the ASCII values of the characters to sort the names lexicographically.
8. If the directory does not exist, output "No such directory!"
9. If reveal is passed too many arguments, output "reveal: Invalid Syntax!"
10. If `reveal -` is run before any `hop` commands have been run since the start of the shell, output "No such directory!"

#### Example

```
<rudy@iiit:~> reveal ~
osnmp1
<rudy@iiit:~> hop ..
<rudy@iiit:/home> reveal
rudy
<rudy@iiit:/home> hop rudy/osnmp1
<rudy@iiit:~/osnmp1> reveal -la
.git
.gitignore
Makefile
README.md
include
llm_completions
src
shell.out
<rudy@iiit:~/osnmp1> reveal -lalalalaaaalal -lalala -al
.git
.gitignore
Makefile
README.md
include
llm_completions
src
shell.out
<rudy@iiit:~/osnmp1> reveal -aaaaaaa -a
.git .gitignore include llm_completions src shell.out Makefile README.md
```

### B.3: log \[30\]

**Syntax**: `log (purge | execute <index>)?`

**Purpose**: The log command allows a user to view their recently executed commands.

#### Requirements

1. The stored list of commands must persist across shell sessions.
2. Store a maximum of 15 commands. Overwrite the oldest command.
3. Do not store a command if it is identical to the previously executed command in the log. Here identical can mean syntactically or exactly. Take it to mean exactly.
4. Always store the entire `shell_cmd` as defined in the CFG.
5. Do not store any `shell_cmd` if the command name of an atomic command is log itself.
6. The command exhibits three behaviors:
   - No arguments: Print the stored commands in order of oldest to newest.
   - `purge`: Clear the history.
   - `execute <index>`: Execute the command at the given index (one-indexed, indexed in order of newest to oldest). Do not store the executed command.
7. If the syntax is incorrect (Ex: `log log`), print "log: Invalid Syntax!"
  
EDIT - I recommend implementing this command after implementing part C.1.

#### Example

```
<rudy@iiit:~> reveal ~
osnmp1
<rudy@iiit:~> hop ..
<rudy@iiit:/home/rudy> reveal
osnmp1
<rudy@iiit:/home/rudy> hop
<rudy@iiit:~> log
reveal ~
hop ..
reveal
hop
<rudy@iiit:~> log execute 2
osnmp1
<rudy@iiit:~> log
reveal ~
hop ..
reveal
hop
<rudy@iiit:~> log purge
<rudy@iiit:~> log
<rudy@iiit:~>
```

## Part C: File Redirection and Pipes \[Total: 200\]

For this part, you will implement I/O redirection and command piping. When processing commands with sequential (`;`) or background (`&`, `&&`) operators, you should only execute the first `cmd_group` and ignore the rest for now.

### C.1: Command Execution

This part was implicitly required, and has just been added explicitly for clarity. You must allow the execution of **arbitrary comands**. This includes commands like `cat`, `echo`, `sleep`, etc. If a command does not exist (example `dosakdaoskdos`), output `Command not found!`.

### C.2: Input Redirection \[50\]

**Syntax**: `command < filename`

**Purpose**: The input redirection operator allows a command to read its standard input from a file instead of the terminal.

#### Requirements

1. The shell must open the specified file for reading using the `open()` system call with `O_RDONLY` flag.
2. If the file does not exist or cannot be opened, the shell must print "No such file or directory" and not execute the command.
3. The shell must redirect the command's standard input (`STDIN_FILENO`) to the opened file using `dup2()`.
4. The shell must close the original file descriptor after duplication to avoid file descriptor leaks.
5. When multiple input redirections are present (e.g., `command < file1 < file2`), only the last one must take effect.

### C.3: Output Redirection \[50\]

**Syntax**: `command > filename` or `command >> filename`

**Purpose**: The output redirection operators allow a command to write its standard output to a file instead of the terminal.

#### Requirements

1. For `>`, the shell must create a new file (wipe it if it already exists) and open it for writing.
2. For `>>`, the shell must append to the passed file (or create if it doesn't exist) and open it for appending.
3. When multiple output redirections are present (e.g., `command > file1 > file2`), only the last one must take effect.
4. Input and output redirection must work together (e.g., `command < input.txt > output.txt`).
5. If the output file cannot be created for some reason, the shell must print "Unable to create file for writing" and not execute the command.

### C.4: Command Piping \[100\]

**Syntax**: `command1 | command2 | ... | commandN`

**Purpose**: The pipe operator allows the standard output of one command to be connected to the standard input of the next command.

#### Requirements

1. The shell must create pipes using the `pipe()` system call for each `|` operator in the command.
2. For each command in the pipeline, the shell must fork a child process.
3. The shell must redirect the standard output of `command[i]` to the write end of `pipe[i]`.
4. The shell must redirect the standard input of `command[i+1]` to the read end of `pipe[i]`.
5. The parent shell must wait for all commands in the pipeline to complete.
6. A piped command sequence is considered finished only when all processes in the pipeline have exited.
7. If any command in the pipeline fails to execute, the pipeline must still attempt to run the remaining commands.
8. File redirection and pipes must work together (e.g., `command1 < input.txt | command2 > output.txt`).

## Part D: Sequential and Background Execution \[Total: 200\]

### D.1: Sequential Execution \[100\]

**Syntax**: `command1 ; command2 ; ... ; commandN`

**Purpose**: The semicolon operator allows multiple commands to be executed one after another.

#### Requirements

1. The shell must execute each command in the order they appear.
2. The shell must wait for each command to complete before starting the next.
3. If a command fails to execute, the shell must continue executing the subsequent commands.
4. Each command in the sequence must be treated as a complete `shell_cmd` as defined in the grammar.
5. The shell prompt must only be displayed after all commands in the sequence have finished executing.

### D.2: Background Execution \[100\]

**Syntax**: `command &`

**Purpose**: The ampersand operator allows a command to run in the background while the shell continues to accept new commands.

#### Requirements

1. When a command ends with `&`, the shell must fork a child process but not wait for it to complete.
2. The shell must print the background job number and process ID in the format: `[job_number] process_id`
3. The shell must immediately display a new prompt after launching the background process.
4. After an user inputs, before parsing the input, the shell must check for completed background processes.
5. When a background process completes successfully, the shell must print: `command_name with pid process_id exited normally`
6. When a background process exits abnormally, the shell must print: `command_name with pid process_id exited abnormally`
7. Background processes must not have access to the terminal for input.

## Part E: Exotic Shell Intrinsics \[Total: 110\]

### E.1: activities \[20\]

**Syntax**: `activities`

**Purpose**: The activities command lists all processes spawned by the shell that are still running or stopped.

#### Requirements

1. The command must display each process in the format: `[pid] : command_name - State`
2. The command must sort the output lexicographically by command name before printing.
3. The command must remove processes from the list once they have terminated.
4. Running processes must show state as "Running" and stopped processes as "Stopped".

### E.2: ping \[20\]

**Syntax**: `ping <pid> <signal_number>`

**Purpose**: The ping command sends a signal to a process with the specified PID.

#### Requirements

1. The command must take the signal number modulo 32 before sending: `actual_signal = signal_number % 32`
2. If the process does not exist, the command must print "No such process found"
3. On successful signal delivery, the command must print "Sent signal signal_number to process with pid `<pid>`"
4. If the inputted `signal_number` is not a valid number, print `Invalid syntax!`.

### E.3: Ctrl-C, Ctrl-D and Ctrl-Z \[30\]

**Purpose**: These keyboard shortcuts provide job control functionality.

#### Requirements for Ctrl-C (SIGINT)

1. The shell must install a signal handler for SIGINT.
2. The handler must send SIGINT to the current foreground child process group if one exists.
3. The shell itself must not terminate on Ctrl-C.

#### Requirements for Ctrl-D (EOF)

1. The shell must detect the EOF condition.
2. The shell must send SIGKILL to all child processes.
3. The shell must exit with status 0.
4. The shell must print "logout" before exiting.

#### Requirements for Ctrl-Z (SIGTSTP)

1. The shell must install a signal handler for SIGTSTP.
2. The handler must send SIGTSTP to the current foreground child process group if one exists.
3. The shell must move the stopped process to the background process list with status "Stopped".
4. The shell must print: `[job_number] Stopped command_name`
5. The shell itself must not stop on Ctrl-Z.

### E.4: fg and bg \[40\]

**Syntax**: `fg [job_number]` and `bg [job_number]`

**Purpose**: The fg and bg commands control background and stopped jobs.

#### Requirements for fg command

1. The command must bring a background or stopped job to the foreground.
2. If the job is stopped, the command must send SIGCONT to resume it.
3. The shell must wait for the job to complete or stop again.
4. If no job number is provided, the command must use the most recently created background/stopped job.
5. If the job number doesn't exist, the command must print "No such job"
6. The command must print the entire command when bringing it to foreground.

#### Requirements for bg command

1. The command must resume a stopped background job by sending SIGCONT.
2. The job must continue running in the background after receiving the signal.
3. The command must print `[job_number] command_name &` when resuming.
4. If the job is already running, the command must print "Job already running"
5. If the job number doesn't exist, the command must print "No such job"
6. Only stopped jobs can be resumed with bg; running jobs must produce "Job already running"
