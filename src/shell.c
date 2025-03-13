#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#define SPECIAL_PIPE '|' 
#define SPECIAL_REDIR '>'
#define SPECIAL_BG '&'

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
    /* Capture the special character and terminate the current token */
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

// Job structure and globals
typedef struct {
    int job_id;
    pid_t pid;
    char command[256];
    int active;
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;

// Job management routines
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
    for (int i = 0; i < job_count; i++) {
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
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].active)
            printf("[Job %d] Running: %s (PID: %d)\n", jobs[i].job_id, jobs[i].command, jobs[i].pid);
    }
}

void bringJobToForeground(int job_id) {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].active && jobs[i].job_id == job_id) {
            printf("Bringing job %d to foreground: %s\n", job_id, jobs[i].command);
            waitpid(jobs[i].pid, NULL, 0);
            jobs[i].active = 0;
            return;
        }
    }
    printf("fg: job %d not found\n", job_id);
}

// Command execution wrapper
void runCommand(const char *command, char **args, int in_fd, int out_fd, int background) {
    pid_t pid = fork();
    if (pid == 0) { // Child process
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
    } else if (pid > 0) { // Parent process
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

// Tokenize a single command into arguments (no triple nesting here)
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
    int in_fd = STDIN_FILENO, pipe_fd[2];
    for (int i = 0; i < commandCount; i++) {
        char *args[MAX_ARGS];
        // Tokenize the current command segment
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
    segment = skipWhitespace(segment);
    char *end = segment;
    while (*end && *end != SPECIAL_PIPE && *end != SPECIAL_REDIR && *end != SPECIAL_BG) {
        end++;
    }
    if (*end) {
        end = handleRedirection(end, backgroundFlag, output_fd);
    }
    return end;
}

// Parse the full input line into command segments, background flags, and output redirection
int parseInputLine(char *input, char **commands, int *backgroundFlags, int *output_fd) {
    int count = 0;
    *output_fd = STDOUT_FILENO;
    char *ptr = input;

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

// Process and execute the parsed input line
void processInput(char *input) {
    updateJobStatus();

    if (strncmp(input, "jobs", 4) == 0) {
        displayJobs();
        return;
    }
    if (strncmp(input, "fg", 2) == 0) {
        int job_id = atoi(input + 3);
        bringJobToForeground(job_id);
        return;
    }

    char *commands[MAX_ARGS];
    int backgroundFlags[MAX_ARGS] = {0};
    int output_fd;
    int commandCount = parseInputLine(input, commands, backgroundFlags, &output_fd);

    executeCommands(commands, commandCount, backgroundFlags, output_fd);
    if (output_fd != STDOUT_FILENO) close(output_fd);
}

int main(void) {
    char input[1024];
    while (1) {
        printf("shell> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0';
        if (strcmp(input, "exit") == 0) break;
        processInput(input);
    }
    return 0;
}