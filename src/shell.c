#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64
#define MAX_JOBS 16

typedef int bool;
#define true 1
#define false 0

// Job Structure
typedef struct job {
    int id;
    pid_t pid;
    char command[MAX_INPUT_SIZE];
    int status;  // 0: Running, 1: Stopped
} job;

job jobs[MAX_JOBS];
int job_count = 0;
pid_t shell_pgid;
int shell_terminal;

// Function Prototypes
void execute_pipe(char *input);
void print_startup_message();
void parse_and_execute(char *input, char *args[]);
void execute_command(char **args, int background, int input_fd, int output_fd);
void handle_redirection(char **args, int *input_fd, int *output_fd);
void add_job(pid_t pid, char *command);
void remove_job(pid_t pid);
void list_jobs();
void bring_to_foreground(int job_id);
void sigchld_handler(int s);
void setup_shell();

void print_startup_message() {
    printf("SSS Shell v0.2\n");
    printf("Type 'exit' to exit\n");
}

// Setup shell for job control
void setup_shell() {
    shell_terminal = STDIN_FILENO;
    shell_pgid = getpid();
    setpgid(shell_pgid, shell_pgid);
    tcsetpgrp(shell_terminal, shell_pgid);

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);
}

int main() {
    char input_buffer[MAX_INPUT_SIZE];
    char *args[MAX_ARGS];

    setup_shell();
    print_startup_message();

    while (1) {
        printf("§ ");
        fflush(stdout);

        if (fgets(input_buffer, MAX_INPUT_SIZE, stdin) == NULL) {
            printf("\n");
            break;
        }

        input_buffer[strcspn(input_buffer, "\n")] = 0;
        if (input_buffer[0] == '\0') {
            continue;
        }

        parse_and_execute(input_buffer, args);
    }
    return 0;
}

// Handle Input/Output Redirection
void handle_redirection(char **args, int *input_fd, int *output_fd) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            *input_fd = open(args[i + 1], O_RDONLY);
            args[i] = NULL;
        } else if (strcmp(args[i], ">") == 0) {
            *output_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            args[i] = NULL;
        } else if (strcmp(args[i], ">>") == 0) {
            *output_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            args[i] = NULL;
        }
    }
}

// Parse and Execute Commands
void parse_and_execute(char *input, char *args[]) {
    char *token;
    int i = 0, background = 0;
    char input_copy[MAX_INPUT_SIZE];
    strcpy(input_copy, input);

    token = strtok(input, " ");
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    args[i] = NULL;

    if (args[0] == NULL) return;

    for (int j = 0; args[j] != NULL; j++) {
        if (strcmp(args[j], "&") == 0) {
            background = 1;
            args[j] = NULL;
            break;
        }
    }

    if (strcmp(args[0], "exit") == 0) {
        exit(0);
    } else if (strcmp(args[0], "jobs") == 0) {
        list_jobs();
    } else if (strcmp(args[0], "fg") == 0) {
        if (args[1] != NULL) {
            bring_to_foreground(atoi(args[1]));
        } else {
            fprintf(stderr, "fg: job number missing\n");
        }
    } else {
        int input_fd = STDIN_FILENO, output_fd = STDOUT_FILENO;
        handle_redirection(args, &input_fd, &output_fd);
        execute_command(args, background, input_fd, output_fd);
    }
}

// 🚀 FIXED: Execute Command
void execute_command(char **args, int background, int input_fd, int output_fd) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        setpgid(0, 0);
        dup2(input_fd, STDIN_FILENO);
        dup2(output_fd, STDOUT_FILENO);

        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        execvp(args[0], args);
        perror("execvp failed");
        exit(1);
    } else if (pid > 0) {
        if (!background) {
            // Foreground process: Wait for completion
            tcsetpgrp(shell_terminal, pid);
            int status;
            waitpid(pid, &status, WUNTRACED);
            tcsetpgrp(shell_terminal, shell_pgid);
        } else {
            // ✅ FIX: Properly detach background process
            setpgid(pid, pid);  // Make sure process runs in its own group
            add_job(pid, args[0]);
            printf("[%d] %d\n", job_count, pid);
            fflush(stdout); // ✅ Ensure next command runs immediately
        }
    }    
}

// Signal handler for SIGCHLD (No Zombie Processes)
void sigchld_handler(int s) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            remove_job(pid); // Only remove if process actually finished
        }
    }
}

// Job Control Functions
void add_job(pid_t pid, char *command) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].id = job_count + 1;
        jobs[job_count].pid = pid;
        strcpy(jobs[job_count].command, command);
        jobs[job_count].status = 0; // Running
        job_count++;
    }
}

void remove_job(pid_t pid) {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].pid == pid) {
            for (int j = i; j < job_count - 1; j++) {
                jobs[j] = jobs[j + 1];
            }
            job_count--;
            break;
        }
    }
}

void list_jobs() {
    printf("Running jobs:\n");
    for (int i = 0; i < job_count; i++) {
        printf("[%d] %d %s\n", jobs[i].id, jobs[i].pid, jobs[i].command);
    }
}

void bring_to_foreground(int job_id) {
    if (job_id > 0 && job_id <= job_count) {
        pid_t pid = jobs[job_id - 1].pid;
        tcsetpgrp(shell_terminal, pid);
        kill(pid, SIGCONT);  // Resume process if stopped

        // Wait for foreground job completion
        int status;
        waitpid(pid, &status, WUNTRACED);

        // Restore shell as foreground
        tcsetpgrp(shell_terminal, shell_pgid);
    } else {
        fprintf(stderr, "fg: No such job\n");
    }
}