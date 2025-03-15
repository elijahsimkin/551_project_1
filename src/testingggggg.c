#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <termios.h>

#define SPECIAL_PIPE '|' 
#define SPECIAL_REDIR '>'
#define SPECIAL_BG '&'
#define MAX_INPUT 1024
#define MAX_HISTORY 100

char history[MAX_HISTORY][MAX_INPUT];  
int history_index = 0;  
int history_pos = -1;   

void enableRawMode() {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag &= ~(ICANON | ECHO);  
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void disableRawMode() {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag |= (ICANON | ECHO);  
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void printCommand(const char *cmd, int *pos) {
    printf("\r\033[K"); 
    printf("shell> %s", cmd);
    fflush(stdout);
    *pos = strlen(cmd);
}

static inline char *skipWhitespace(char *ptr) {
    while (*ptr == ' ' || *ptr == '\t') {
        ptr++;
    }
    return ptr;
}

static inline char *skipWhitespaceAndPipes(char *ptr) {
    while (*ptr == ' ' || *ptr == '\t' || *ptr == '|') {
        ptr++;
    }
    return ptr;
}

static char *handleRedirection(char *ptr, int *backgroundFlag, int *output_fd) {
    char special = *ptr;
    *ptr = '\0';
    ptr++;
    
    if (special == SPECIAL_REDIR) {
        ptr = skipWhitespace(ptr);
        char *filename = ptr;
        while (*ptr && *ptr != ' ' && *ptr != '\t') {
            ptr++;
        }
        if (*ptr) {
            *ptr = '\0';
            ptr++;
        }
        *output_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (*output_fd < 0) {
            perror("open failed");
        }
    } else if (special == SPECIAL_BG) {
        *backgroundFlag = 1;
    }
    return ptr;
}

#define MAX_ARGS 128
#define MAX_JOBS 64
#define PIPE_READ  0
#define PIPE_WRITE 1

typedef struct {
    int job_id;
    pid_t pid;
    char command[256];
    int active;
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;

void addJob(pid_t pid, const char *cmd) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].job_id = job_count + 1;
        jobs[job_count].pid = pid;
        strncpy(jobs[job_count].command, cmd, sizeof(jobs[job_count].command) - 1);
        jobs[job_count].command[sizeof(jobs[job_count].command) - 1] = '\0';
        jobs[job_count].active = 1;
        job_count++;
    }
}

void updateJobStatus(void) {
    int  i;
    for (i = 0; i < job_count; i++) {
        if (jobs[i].active) {
            int status;
            if (waitpid(jobs[i].pid, &status, WNOHANG) > 0) {
                printf("[Job %d] Done: %s\n", jobs[i].job_id, jobs[i].command);
                jobs[i].active = 0;
            }
        }
    }
}

void displayJobs(void) {
    int i;
    for (i = 0; i < job_count; i++) {
        if (jobs[i].active)
            printf("[Job %d] Running: %s (PID: %d)\n", jobs[i].job_id, jobs[i].command, jobs[i].pid);
    }
}

void bringJobToForeground(int job_id) {
    int i;
    for (i = 0; i < job_count; i++) {
        if (jobs[i].active && jobs[i].job_id == job_id) {
            printf("Bringing job %d to foreground: %s\n", job_id, jobs[i].command);
            waitpid(jobs[i].pid, NULL, 0);
            jobs[i].active = 0;
            return;
        }
    }
    printf("fg: job %d not found\n", job_id);
}

void handle_cd(char **args) {
    if (args[1] == NULL) {
        const char *home = getenv("HOME");
        if (home == NULL) home = "/";  
        if (chdir(home) != 0) {
            perror("cd failed");
        }
    } else {
        if (chdir(args[1]) != 0) {
            perror("cd failed");
        }
    }
}

void runCommand(const char *command, char **args, int in_fd, int out_fd, int background) {
    pid_t pid = fork();
    if (pid == 0) { 
        if (in_fd != STDIN_FILENO) {
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        }
        if (out_fd != STDOUT_FILENO) {
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }
        execvp(command, args);
        perror("execvp failed");
        _exit(1);
    } else if (pid > 0) { 
        if (in_fd != STDIN_FILENO) close(in_fd);
        if (out_fd != STDOUT_FILENO) close(out_fd);
        if (!background) {
            waitpid(pid, NULL, 0);
        } else {
            addJob(pid, command);
            printf("[Job %d] Started: %s (PID: %d)\n", job_count, command, pid);
        }
    }
}

int tokenizeCommand(char *commandStr, char **tokens) {
    int count = 0;
    char *token = strtok(commandStr, " \t");
    while (token != NULL && count < MAX_ARGS - 1) {
        tokens[count++] = token;
        token = strtok(NULL, " \t");
    }
    tokens[count] = NULL;
    return count;
}

void executeCommands(char **commands, int commandCount, int *backgroundFlags, int output_fd) {
    int in_fd = STDIN_FILENO, pipe_fd[2], i;
    for (i = 0; i < commandCount; i++) {
        char *args[MAX_ARGS];
        tokenizeCommand(commands[i], args);
        if (i < commandCount - 1) {
            if (pipe(pipe_fd) < 0) {
                perror("pipe failed");
                return;
            }
            runCommand(args[0], args, in_fd, pipe_fd[PIPE_WRITE], backgroundFlags[i]);
            in_fd = pipe_fd[PIPE_READ];
        } else {
            runCommand(args[0], args, in_fd, output_fd, backgroundFlags[i]);
            if (in_fd != STDIN_FILENO) close(in_fd);
        }
    }
}

char *parseCommandSegment(char *segment, int *backgroundFlag, int *output_fd) {
    char *end;
    segment = skipWhitespace(segment);
    end = segment;
    while (*end && *end != SPECIAL_PIPE && *end != SPECIAL_REDIR && *end != SPECIAL_BG) {
        end++;
    }
    if (*end) {
        end = handleRedirection(end, backgroundFlag, output_fd);
    }
    return end;
}

int parseInputLine(char *input, char **commands, int *backgroundFlags, int *output_fd) {
    int count = 0;
    char *ptr = input;
    *output_fd = STDOUT_FILENO;

    while (*ptr) {
        ptr = skipWhitespace(ptr);
        if (!*ptr) break;
        commands[count] = ptr;
        backgroundFlags[count] = 0;
        ptr = parseCommandSegment(ptr, &backgroundFlags[count], output_fd);
        count++;
        ptr = skipWhitespaceAndPipes(ptr);
    }
    commands[count] = NULL;
    return count;
}

void processInput(char *input) {
    int job_id;
    char *args[MAX_ARGS], *commands[MAX_ARGS];
    int backgroundFlags[MAX_ARGS] = {0};
    int output_fd, commandCount;

    updateJobStatus();

    if (strncmp(input, "jobs", 4) == 0) {
        displayJobs();
        return;
    }
    if (strncmp(input, "fg", 2) == 0) {
        job_id = atoi(input + 3);
        bringJobToForeground(job_id);
        return;
    }
    if (strncmp(input, "cd", 2) == 0) {
        tokenizeCommand(input, args);
        handle_cd(args);
        return;
    }
    
    commandCount = parseInputLine(input, commands, backgroundFlags, &output_fd);

    executeCommands(commands, commandCount, backgroundFlags, output_fd);
    if (output_fd != STDOUT_FILENO) close(output_fd);
}

int main() {
    char input[MAX_INPUT];
    int pos;
    char c, arrow;

    enableRawMode();

    while (1) {
        printf("shell> ");
        fflush(stdout);
        
        pos = 0;
        memset(input, 0, sizeof(input));

        while (1) {
            c = getchar();

            if (c == '\n') {  
                input[pos] = '\0';
                break;
            } else if (c == EOF) {
                printf("\n");
                exit(0);  
            } else if (c == '\x1B') {  
                getchar();  
                arrow = getchar();
                if (arrow == 'A') {  
                    if (history_pos > 0) {
                        history_pos--;
                        strcpy(input, history[history_pos]);
                        printCommand(input, &pos);
                    }
                } else if (arrow == 'B') {  
                    if (history_pos < history_index - 1) {
                        history_pos++;
                        strcpy(input, history[history_pos]);
                        printCommand(input, &pos);
                    } else {
                        history_pos = history_index;
                        memset(input, 0, sizeof(input));
                        printCommand("", &pos);
                    }
                } else if (arrow == 'D') {  
                    if (pos > 0) {
                        pos--;
                        printf("\b");
                        fflush(stdout);
                    }
                } else if (arrow == 'C') {  
                    if (pos < strlen(input)) {
                        printf("%c", input[pos]);
                        fflush(stdout);
                        pos++;
                    }
                }
            } else if (c == '\x7F') {  
                if (pos > 0) {
                    pos--;
                    input[pos] = '\0';
                    printf("\b \b");
                    fflush(stdout);
                }
            } else {  
                input[pos++] = c;
                putchar(c);
                fflush(stdout);
            }
        }

        if (pos == 0) continue;  
        if (strcmp(input, "exit") == 0) break;  

        strncpy(history[history_index % MAX_HISTORY], input, MAX_INPUT - 1);
        history_index++;
        history_pos = history_index;

        printf("\n");
        processInput(input);
    }

    disableRawMode();
    return 0;
}