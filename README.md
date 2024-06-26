# Shell

This project is an implementation of a shell that runs as an application program on top of the Linux kernel. The shell accepts user commands (one line at a time) and executes them much like the default linux bash terminal.

## Features Implemented
The following features have been implemented:

* Run an external command
* Run an external command by redirecting standard input/ output from/ to a file
* Combination of input and output redirection
* Run an external command in the background with possible input and output redirections
* Run several external commands in the pipe mode along with input and output redirections
* Interrupting commands running in your shell (`Ctrl-C`)
* Move a command in execution to the background (`Ctrl-Z`)
* Implementing cd and pwd
* Handling wildcards in commands (* and ?)
* Implementing searching through history using up/down arrow keys
* Implementing cursor movement using left/right arrow keys
* Cursor moves to start/end of line using `Ctrl+A` and `Ctrl+E`

I have tried to keep the shell as close to the bash terminal as possible, so the syntax of these commands are similar to their bash counterparts.

## Additional Features
The following additional features have been implemented (not in normal BASH):

### sb (squash bug):
Detects a simple malware process that spawns multiple child processes and remains in an idle state. The command creates a child process which identifies the parent, grandparent, and so on of the suspected process and suggests the root cause of the issue based on a heuristic. The heuristic is explained in the [squashbug.heuristic.txt](squashbug.heuristic.txt) file.
Usage: `sb [-suggest] <pid>`

### delep (delete with extreme prejudice):
Helps to delete a file that has been locked by a process. The command takes a filepath as an argument, lists all the process pids which have the file open and those which are holding a lock over the file, and prompts the user to kill each of those processes. 
Usage: `delep <filepath>`

Note: For "sb" command, the [squashbug.heuristic.txt](squashbug.heuristic.txt) file explains the heuristic used to detect the malware process.

## Usage

To run the shell, run the following commands in the terminal:

1. Install readline library:

```bash
    sudo apt-get install libreadline-dev
```

2. Compile and run the shell:

```bash
    g++ shell.cpp -o shell -lreadline
    ./shell
```


Or, simply use the Makefile:

```bash
    sudo make
    ./shell
```