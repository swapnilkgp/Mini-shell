/*
Run with command:- 
g++ shell.cpp -o shell -lreadline ; ./shell
*/

#include <dirent.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <wait.h>

#include <deque>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <limits>


using namespace std;

#define RESET "\x1B[0m"
#define ALERT_LIMIT 50
#define BOLD "\x1B[1m"
#define GREEN "\033[1;32m"
#define BLUE "\033[1;34m"
#define MAXCMDLEN 1024
#define HISTSIZE 1000

char curr_working_dir[1024]; // Current working directory
char prompt[1124];
int BACKGROUND_FLAG;                              // Flag to check if the command is to be run in background
vector<pair<pid_t, string>> background_processes; // Vector to store the PIDs of background processes
pid_t  current_waiting_process=-1; // PID of the process that is currently being waited for


deque<string> hist;
string command;
char *input;
FILE *fptr;


bool match(char *pattern, char *str)
{
    // If we reach at the end of both strings, we are done
    if (*pattern == '\0' && *str == '\0')
        return true;

    // Make sure to eliminate consecutive '*'
    if (*pattern == '*')
    {
        while (*(pattern + 1) == '*')
            pattern++;
    }

    // Make sure that the characters after '*' are present
    // in str string. This function assumes that the
    // pattern string will not contain two consecutive '*'
    if (*pattern == '*' && *(pattern + 1) != '\0' && *str == '\0')
        return false;

    // If the pattern string contains '?', or current
    // characters of both strings match
    if (*pattern == '?' || *pattern == *str)
        return match(pattern + 1, str + 1);

    // If there is *, then there are two possibilities
    // a) We consider current character of str string
    // b) We ignore current character of str string.
    if (*pattern == '*')
        return match(pattern + 1, str) || match(pattern, str + 1);
    return false;
}

vector<char *> get_filenames(char *pattern)
{
    DIR *dir;
    struct dirent *ent;
    vector<char *> filenames;

    if ((dir = opendir(".")) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            char *fname = (char *)malloc(sizeof(char) * (strlen(ent->d_name) + 1));
            strcpy(fname, ent->d_name);
            bool is_matching = match(pattern, fname);
            if (is_matching)
            {
                filenames.push_back(fname);
            }
            else
            {
                free(fname);
            }
        }
        closedir(dir);
    }
    else
    {
        cerr << "Error reading current directory" << endl;
        return {};
    }

    return filenames;
}

bool contains_wildcard(const string &string)
{
    for (char c : string)
    {
        if (c == '*' || c == '?')
        {
            return true;
        }
    }
    return false;
}

// substitutes wildcards in the pattern with the actual filenames
vector<char *> substitute(char *pattern)
{
    if (!contains_wildcard(pattern))
    {
        char *temp = (char *)malloc(sizeof(char) * (strlen(pattern) + 1));
        strcpy(temp, pattern);
        return {temp};
    }
    vector<char *> filenames = get_filenames(pattern);
    return filenames;
}




const char *printPrompt();            // Function to print the prompt
void check_background_processes();    // Function to check if any background process has finished

// Function to handle the SIGINT signal (Ctrl+C) and print the prompt after that
void sig_handler_prompt(int signum) {
    string tmpc = rl_line_buffer;
    rl_replace_line("", 0);
    rl_redisplay();
    rl_point = rl_end;
    printf("%s%s^C", printPrompt(), tmpc.c_str());
    printf("\n%s", printPrompt());
}

// Function to handle the SIGINT signal (Ctrl+C) and not print the prompt after that
void sig_handler_no_prompt(int signum) {
    printf("\n");
}

void sig_handler_ctrl_Z(int signum) {
    if (current_waiting_process != -1) {
        BACKGROUND_FLAG = 1;
        background_processes.push_back(make_pair(current_waiting_process, command));
        printf("\n[%ld] %d\n", background_processes.size(), current_waiting_process);
        fflush(stdout);
    } else {
        string tmpc = rl_line_buffer;
        rl_replace_line("", 0);
        rl_redisplay();
        rl_point = rl_end;
        printf("%s%s^Z", printPrompt(), tmpc.c_str());
        printf("\n%s", printPrompt());
    }
    for (auto &pr : background_processes) {
        if (pr.first == -1)
            continue;    // process already finished (waitpid() was called
        kill(pr.first, SIGCONT);
    }
}





int cm, sz;

int up_arrow_function(int value, int count)
{
    if (cm < sz)
    {
        hist.at(sz - cm) = rl_line_buffer;
        cm++;
        rl_replace_line(hist.at(sz - cm).c_str(), 0);
        rl_point = rl_end;
        rl_redisplay();
    }
    return 0;
}

int down_arrow_function(int value, int count)
{
    if (cm)
    {
        hist.at(sz - cm) = rl_line_buffer;
        cm--;
        rl_replace_line(hist.at(sz - cm).c_str(), 0);
        rl_point = rl_end;
        rl_redisplay();
    }
    return 0;
}

string getcmd()
{
    cm = 0;
    sz = hist.size();
    hist.push_back("");
    rl_command_func_t up_arrow_function, down_arrow_function;
    rl_bind_keyseq("\033[A", up_arrow_function);
    rl_bind_keyseq("\033[B", down_arrow_function);
    input = readline(printPrompt());

    command = input;
    hist.at(sz) = command;
    if (sz && hist.at(sz - 1) == command)
        hist.pop_back();
    free(input);

    return command;
}






int get_cpu_util(pid_t pid) {
    string line;
    string path = "/proc/" + to_string(pid) + "/stat";
    ifstream stat_stream(path);
    if (!stat_stream.is_open()) {
        cerr << "Error: failed to open file '" << path << "'" << endl;
    }
    getline(stat_stream, line);
    istringstream line_stream(line);

    // The CPU utilization is stored as the 14th and 15th values in the line.
    // The memory utilization is stored as the 22nd value in the line.
    long long utime, stime, start_time, uptime;
    for (int i = 0; i < 23; ++i) {
        if (i == 13)
            line_stream >> utime;
        else if (i == 14)
            line_stream >> stime;
        else if (i == 22) {
            line_stream >> start_time;
            // cout << "reading start time " << start_time << "\n";
        } else
            line_stream.ignore(numeric_limits<streamsize>::max(), ' ');
    }

    string path2 = "/proc/uptime";
    ifstream stat_stream2(path2);
    if (!stat_stream2.is_open()) {
        cerr << "Error: failed to open file '" << path2 << "'" << endl;
    }
    getline(stat_stream2, line);
    istringstream line_stream2(line);
    line_stream2 >> uptime;
    uptime *= 100;    // converting to clock ticks
    double cpu_util = (stime + utime) * 100.0 / (uptime - start_time);

    // Print the results.
    // cout << "utime and stime " << utime << " + " << stime << endl;
    // cout << "Start Time: " << start_time << endl;
    // cout << "Uptime: " << uptime << " " << endl;
    // cout << "CPU Utilization: " << cpu_util << "\n";
    return cpu_util;
}

vector<string> list_dir(string path) {
    vector<string> list_dirs;
    DIR *d;
    struct dirent *dir;
    d = opendir(path.c_str());
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            list_dirs.push_back(dir->d_name);
        }
        closedir(d);
    }
    return list_dirs;
}

int get_heur(pid_t pid) {
    int heur = 0, child_util;
    vector<string> list_dirs;
    string path = "/proc/" + to_string(pid) + "/task";
    list_dirs = list_dir(path);

    for (int i = 0; i < list_dirs.size(); i++) {
        if (i == 0 || i == 1) continue;
        string tid = list_dirs[i];
        // cout << tid << " ";
        path = "/proc/" + to_string(pid) + "/task/" + tid + "/children";
        ifstream stat_stream(path);
        if (!stat_stream.is_open()) {
            cerr << "Error: failed to open file '" << path << "'" << endl;
            return 2;
        }
        string line;
        getline(stat_stream, line);
        istringstream line_stream(line);
        pid_t cpid;

        while (!line_stream.eof()) {
            line_stream >> cpid;
            if (cpid <= 0) continue;
            child_util = get_cpu_util(cpid);
            cout << "child " << cpid << " utilization " << child_util << "%\n";
            heur += child_util;
        }
    }
    return heur;
}

char *nextArg(char *&stringp) {
    while (*stringp == ' ' || *stringp == '\t') stringp++;
    if (*stringp == '\0') return NULL;
    char *arg = stringp;

    if (*arg == '"') {
        arg++;
        stringp++;
        while (1) {    //*stringp != '"' && *stringp != '\0'){
            if (*stringp == '\\') {
                if (*(stringp + 1) == '"' || *(stringp + 1) == '\'' || *(stringp + 1) == '\\') {
                    strcpy(stringp, stringp + 1);
                }
                stringp++;
            }
            if (*stringp == '"') break;
            if (*stringp == '\0') break;
            stringp++;
        }
        if (*stringp == '"') *stringp++ = '\0';
        return arg;
    }

    if (*arg == '\'') {
        arg++;
        stringp++;
        while (1) {    //*stringp != '"' && *stringp != '\0'){
            if (*stringp == '\\') {
                if (*(stringp + 1) == '\'' || *(stringp + 1) == '"' || *(stringp + 1) == '\\') {
                    strcpy(stringp, stringp + 1);
                }
                stringp++;
            }
            if (*stringp == '\'') break;
            if (*stringp == '\0') break;
            stringp++;
        }
        if (*stringp == '\'') *stringp++ = '\0';
        return arg;
    }

    while (*stringp != ' ' && *stringp != '\t' && *stringp != '\0')
        stringp++;

    if (*stringp != '\0') *stringp++ = '\0';
    return arg;
}

// gets arguments from a single command
void getArgs(char *stringp, vector<char *> &args, int &fInRedirect, int &fOutRedirect) {
    while (1) {
        char *arg = nextArg(stringp);
        if (arg == NULL)
            break;
        if (strlen(arg) == 0)
            continue;
        else if (strcmp(arg, "&") == 0) {
            BACKGROUND_FLAG = 1;
        } else {
            int i = 0, j = 0;
            // check for i/o redirection(s) in extracted tokens
            while (arg[j] != '\0') {
                if (arg[j] == '<') {
                    if (i != j) {
                        arg[j] = '\0';
                        args.push_back(arg + i);
                    }
                    fInRedirect = args.size();
                    i = j + 1;
                } else if (arg[j] == '>') {
                    if (i != j) {
                        arg[j] = '\0';
                        args.push_back(arg + i);
                    }
                    fOutRedirect = args.size();
                    i = j + 1;
                }
                j++;
            }
            if (i != j) {
                char *word = arg + i;
                vector<char *> substitutes = substitute(word);
                for (char *substitute : substitutes)
                    args.push_back(substitute);
            }
        }
    }
}

// Function to execute pwd
void executePwd() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("Error: Unable to get current working directory\n");
    }
}

// Function to get pids of processes having file open or holding lock over file
void get_pids(string filepath, vector<pid_t> &pids) {
    string ex = "lsof -t " + filepath;
    FILE *fp = popen(ex.c_str(), "r");
    if (fp == NULL) {
        perror("Error running lsof");
        return;
    }
    int p;
    while (fscanf(fp, "%d", &p) == 1) {
        pids.push_back(p);
    }
    pclose(fp);
}

// Function to execute delep **delete with extreme prejudice**
void execeuteDelep(vector<char *> &args) {
    if (args.size() != 2) {
        perror("Syntax error: Usage: delep <<filepath>>\n");
        return;
    }
    vector<pid_t> pids;
    get_pids(args[1], pids);
    bool consent = 0;
    if (pids.empty()) {
        printf("No process found with open file: %s\n", args[1]);
        if (remove(args[1]) != 0)
            perror("Error deleting file\n");
        else
            printf("%s deleted succesfully!\n", args[1]);
    } else {
        printf("The following processes have the file open or are holding a lock:\n");
        for (auto &p : pids) printf("%d ", p);
        char ans[MAX_INPUT];
        printf("\nDo you want to kill all these processes and delete file? (yes/no): ");
        scanf("%s", ans);
        if (strcmp(ans, "yes") == 0) {
            for (auto &p : pids) {
                // Function to kill process with given pid
                kill(p, SIGKILL);
            }
            if (remove(args[1]) != 0)
                perror("Error deleting file\n");
            else
                printf("%s deleted succesfully!\n", args[1]);
        }
    }
}

// Function to execute sb **squash bug**
void executeSb(vector<char *> &args) {
    if (args.size() < 2 || args.size() > 3) {
        perror("Syntax error: Usage: sb <<pid>> [--suggest]\n");
        return;
    }
    int pid = atoi(args[1]);
    bool suggest = 0;
    if (args.size() == 3)
        suggest = 1;
    string path, status;
    pid_t ppid, ptid, mlw_pid = -1;
    // ppid -> id of parent process, ptid -> id of controlling terminal

    for (int i = 0; i < 3; i++) {
        // open /proc/[pid]/stat
        path = "/proc/" + to_string(pid) + "/stat";
        ifstream stat_stream(path);
        if (!stat_stream.is_open()) {
            cerr << "Error: failed to open file '" << path << "'" << endl;
            return;
        }

        string line;
        getline(stat_stream, line);
        istringstream line_stream(line);

        for (int i = 0; i < 7; ++i) {
            if (i == 2) line_stream >> status;
            if (i == 3) line_stream >> ppid;
            if (i == 6)
                line_stream >> ptid;
            else
                line_stream.ignore(numeric_limits<streamsize>::max(), ' ');
        }
        cout << "gen  " << i << " pid: " << pid << " status : " << status << endl;

        if (suggest == 1) {
            int heur = get_heur(pid);
            cout << "heuristic " << heur << " status " << status << "\n";
            if (heur > ALERT_LIMIT && status == "S")    // can replace with diff checker_fn()
            {
                mlw_pid = pid;
                break;
            }
        }
        pid = ppid;
    }
    if (suggest == 1) {
        if (mlw_pid > 0)
            cout << "Detected Malware PID : " << mlw_pid << "\n";
        else
            cout << "Malware not found \n";
    }
}

// Function to execute a single commands
void executeSingleCommand(string command) {
    vector<char *> args;
    int fInRedirect = 0, fOutRedirect = 0;
    getArgs((char *)command.c_str(), args, fInRedirect, fOutRedirect);

    if (args.size() == 0)
        return;
    else if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "cd") == 0)
        // Called from child(in case of pipe), so not useful
        exit(EXIT_SUCCESS);
    // handle pwd from shell
    else if (strcmp(args[0], "pwd") == 0) {
        executePwd();
        exit(EXIT_SUCCESS);
    }
    // handle delep from shell
    else if (strcmp(args[0], "delep") == 0) {
        execeuteDelep(args);
        exit(EXIT_SUCCESS);
    }
    // handle sb (squash bug) from shell
    else if (strcmp(args[0], "sb") == 0) {
        executeSb(args);
        exit(EXIT_SUCCESS);
    }

    if (fInRedirect != 0) {
        // open input file
        int in = open(args[fInRedirect], O_RDONLY);
        if (in == -1) {
            cerr << "Error opening file: " << args[fInRedirect] << endl;
            exit(1);
        }
        // copy file_desc to STDIN
        dup2(in, STDIN_FILENO);
        close(in);
    }
    if (fOutRedirect != 0) {
        // open output file
        int out = open(args[fOutRedirect], O_WRONLY | O_TRUNC | O_CREAT, 0644);
        if (out == -1) {
            cerr << "Error opening file: " << args[fInRedirect] << endl;
            exit(1);
        }
        // copy file_desc to STDOUT
        dup2(out, STDOUT_FILENO);
        close(out);
    }

    args.push_back(NULL);
    char **args_ptr = &args[0];
    // Execute arguments
    if (execvp(args[0], args_ptr) < 0) {
        cerr << "Error in executing command" << endl;
    }
}

// Function to change directory
void executeCD(vector<char *> &args) {
    if (args.size() < 2) {
        cerr << "Error: cd: missing argument. Usage: cd <directory>" << endl;
        return;
    } else if (args.size() > 2) {
        cerr << "Error: cd: too many arguments. Usage: cd <directory>" << endl;
        return;
    }

    if (chdir(args[1]) != 0) {
        cerr << "Error: unable to change directory to \"" << args[1] << "\"" << endl;
        return;
    }
}

int execute_our_command(string command) {
    vector<char *> args;
    int fInRedirect = 0, fOutRedirect = 0;
    getArgs((char *)command.c_str(), args, fInRedirect, fOutRedirect);

    // handle exit from shell
    if (strcmp(args[0], "exit") == 0) {
        while (!hist.empty()) {
            fprintf(fptr, "%s\n", hist.front().c_str());
            hist.pop_front();
        }
        fclose(fptr);
        printf("exit\n");
        exit(0);
    }
    // handle cd from shell
    else if (strcmp(args[0], "cd") == 0) {
        executeCD(args);
        return 1;
    }
    return 0;
}

void execute(string command) {
    int len = command.size();
    while (--len >= 0) {
        if (command[len] == '|') {
            pid_t pid = fork();
            if (pid == -1) {
                perror("fork");
                exit(EXIT_FAILURE);
            }

            if (pid == 0) {
                BACKGROUND_FLAG = 0;

                int pipe_fds[2];
                pid_t pid;

                if (pipe(pipe_fds) == -1) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }

                pid = fork();
                if (pid == -1) {
                    perror("fork");
                    exit(EXIT_FAILURE);
                }

                if (pid == 0) {
                    BACKGROUND_FLAG = 0;

                    // 1st process
                    close(pipe_fds[0]);
                    dup2(pipe_fds[1], STDOUT_FILENO);
                    close(pipe_fds[1]);
                    execute(command.substr(0, len));
                    exit(EXIT_SUCCESS);
                }

                else {
                    // 2nd process

                    close(pipe_fds[1]);
                    dup2(pipe_fds[0], STDIN_FILENO);
                    close(pipe_fds[0]);

                    executeSingleCommand(command.substr(len + 1));
                    exit(EXIT_SUCCESS);
                }
            } else {
                // parent process
                // close(pipe_fds[1]);
                // close(pipe_fds[0]);
                if (!BACKGROUND_FLAG) {
                    current_waiting_process = pid;
                    while (!BACKGROUND_FLAG) {
                        int chek = waitpid(pid, NULL, WNOHANG);
                        if (chek == pid) {
                            break;
                        }
                    }
                    current_waiting_process = -1;
                    int status = execute_our_command(command.substr(len + 1));
                } else {
                    background_processes.push_back(make_pair(pid, command));
                    printf("[%ld] %d\n", background_processes.size(), pid);
                    fflush(stdout);
                }

                // if (status == 1)
                //     return;
                return;
            }
        }
    }

    // IF NO PIPE

    // fork child to execute
    pid_t pid = fork();
    if (pid == -1) {
        cerr << "Failed To Fork!" << endl;
        return;
    } else if (pid == 0) {
        BACKGROUND_FLAG = 0;
        executeSingleCommand(command);
        exit(EXIT_SUCCESS);
    } else {
        if (!BACKGROUND_FLAG) {
            current_waiting_process = pid;
            while (!BACKGROUND_FLAG) {
                int chek = waitpid(pid, NULL, WNOHANG);
                if (chek == pid) {
                    break;
                }
            }
            current_waiting_process = -1;
            int status = execute_our_command(command.substr(len + 1));
        } else {
            background_processes.push_back(make_pair(pid, command));
            printf("[%ld] %d\n", background_processes.size(), pid);
            fflush(stdout);
        }
        return;
    }
}

void parseCommand(string &command) {
    BACKGROUND_FLAG = 0;

    while (!command.empty() && (command.back() == ' ' || command.back() == '\t' || command.back() == '\n'))
        command.pop_back();

    if (command.empty())
        return;

    // find first occurance of '&' in command
    size_t found = command.find('&');
    if (found != string::npos) {
        if (found != command.size() - 1) {
            cerr << "Syntax error: tokens found after '&'" << endl;
            return;
        } else {
            command.pop_back();
            BACKGROUND_FLAG = 1;
        }
    }
    execute(command);
}



int main() {
    signal(SIGTSTP, sig_handler_ctrl_Z);    // Ctrl+Z

    fptr = fopen(".history", "a");    // used a option to create a file if doesn't exist
    fclose(fptr);
    fptr = fopen(".history", "r+");    // used r+ option to open file for r/w
    if (!fptr) {
        printf("history couldn't be accesed\n");
    }
    char str[MAXCMDLEN];
    while (fgets(str, MAXCMDLEN, fptr)) {
        int len = strlen(str);
        if (str[len - 1] == '\n')
            str[len - 1] = '\0';
        hist.push_back(str);
    }
    fseek(fptr, 0, SEEK_SET);
    bool f = 0;
    while (true) {
        vector ar = background_processes;
        printPrompt();
        signal(SIGINT, sig_handler_prompt);
        command = getcmd();
        signal(SIGINT, sig_handler_no_prompt);
        parseCommand(command);
        fflush(stdout);
    }
    fclose(fptr);
    return 0;
}

// Function to print the prompt
const char *printPrompt() {
    check_background_processes();
    getcwd(curr_working_dir, sizeof(curr_working_dir));
    sprintf(prompt, "SHELL++:%s$ ", curr_working_dir);
    // sprintf(prompt, "%s%sSHELL++:%s%s$ %s", BOLD, GREEN, BLUE, curr_working_dir, RESET);
    fflush(stdout);
    return prompt;
}

// Function to check if any background process has finished
void check_background_processes() {
    for (int i = 0; i < background_processes.size(); i++) {
        if (background_processes[i].first == -1)
            continue;    // process already finished (waitpid() was called
        int status;
        pid_t pid = waitpid(background_processes[i].first, &status, WNOHANG);
        if (pid > 0) {
            printf("[%d] Done\t\t", i + 1);
            printf("%s\n", background_processes[i].second.c_str());
            fflush(stdout);
            background_processes[i].first = -1;
        }
    }
    while (background_processes.size() && background_processes.back().first == -1)
        background_processes.pop_back();
}




