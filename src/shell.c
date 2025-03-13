#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_ARGS 128
#define MAX_JOBS 64
#define PIPE_READ 0
#define PIPE_WRITE 1

typedef struct {
    int job_id;
    pid_t pid;
    char command[256];
    int active;
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;

void add_job(pid_t pid, char *command) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].job_id = job_count + 1;
        jobs[job_count].pid = pid;
        strncpy(jobs[job_count].command, command, 255);
        jobs[job_count].command[255] = '\0';
        jobs[job_count].active = 1;
        job_count++;
    }
}

void check_jobs() {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].active) {
            int status;
            pid_t result = waitpid(jobs[i].pid, &status, WNOHANG);
            if (result > 0) {
                printf("[Job %d] Done: %s\n", jobs[i].job_id, jobs[i].command);
                jobs[i].active = 0;
            }
        }
    }
}

void list_jobs() {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].active) {
            printf("[Job %d] Running: %s (PID: %d)\n", jobs[i].job_id, jobs[i].command, jobs[i].pid);
        }
    }
}

void foreground_job(int job_id) {
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

void execute_command(char *command, char **args, int input_fd, int output_fd, int background) {
    pid_t pid = fork();
    if (pid == 0) { // Child process
        if (input_fd != STDIN_FILENO) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (output_fd != STDOUT_FILENO) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }
        execvp(command, args);
        perror("execvp failed");
        _exit(1);
    } else if (pid > 0) { // Parent process
        if (input_fd != STDIN_FILENO) close(input_fd);
        if (output_fd != STDOUT_FILENO) close(output_fd);
        
        if (!background) {
            waitpid(pid, NULL, 0);
        } else {
            add_job(pid, command);
            printf("[Job %d] Started: %s (PID: %d)\n", job_count, command, pid);
        }
    }
}

void process_input(char *input) {
    check_jobs();

    if (strncmp(input, "jobs", 4) == 0) {
        list_jobs();
        return;
    }
    if (strncmp(input, "fg", 2) == 0) {
        int job_id = atoi(input + 3);
        foreground_job(job_id);
        return;
    }
    
    char *commands[MAX_ARGS];
    int command_count = 0;
    int background_flags[MAX_ARGS] = {0};
    int output_fd = STDOUT_FILENO;

    char *ptr = input;
    while (*ptr) {
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        if (!*ptr) break;
        commands[command_count] = ptr;
        background_flags[command_count] = 0;
        
        while (*ptr && *ptr != '|' && *ptr != '>' && *ptr != '&') ptr++;
        if (*ptr == '|') {
            *ptr++ = '\0';
        } else if (*ptr == '>') {
            *ptr++ = '\0';
            while (*ptr == ' ' || *ptr == '\t') ptr++;
            char *filename = ptr;
            while (*ptr && *ptr != ' ' && *ptr != '\t') ptr++;
            if (*ptr) *ptr++ = '\0';
            output_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (output_fd < 0) {
                perror("open failed");
                return;
            }
        } else if (*ptr == '&') {
            *ptr++ = '\0';
            background_flags[command_count] = 1;
        }
        command_count++;
    }
    commands[command_count] = NULL;

    int input_fd = STDIN_FILENO;
    int pipe_fd[2];
    
    for (int i = 0; i < command_count; i++) {
        char *token, *command = NULL, *args[MAX_ARGS];
        int arg_count = 0;
        
        char *cmd_ptr = commands[i];
        while (*cmd_ptr) {
            while (*cmd_ptr == ' ' || *cmd_ptr == '\t') cmd_ptr++;
            if (!*cmd_ptr) break;
            token = cmd_ptr;
            while (*cmd_ptr && *cmd_ptr != ' ' && *cmd_ptr != '\t') cmd_ptr++;
            if (*cmd_ptr) {
                *cmd_ptr++ = '\0';
                while (*cmd_ptr == ' ' || *cmd_ptr == '\t') cmd_ptr++;
            }
            if (*token == '\0') continue;
            if (arg_count == 0) command = token;
            args[arg_count++] = token;
            args[arg_count] = NULL;
        }
        
        if (i < command_count - 1) {
            pipe(pipe_fd);
            execute_command(command, args, input_fd, pipe_fd[PIPE_WRITE], background_flags[i]);
            close(pipe_fd[PIPE_WRITE]); // Close write end in parent
            input_fd = pipe_fd[PIPE_READ]; // Pass read end to next command
        } else {
            execute_command(command, args, input_fd, output_fd, background_flags[i]);
            if (input_fd != STDIN_FILENO) close(input_fd); // Close last input_fd
        }
    }
    if (output_fd != STDOUT_FILENO) close(output_fd);
}

int main() {
    char input[1024];

    while (1) {
        printf("shell> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0; // Remove newline
        if (strcmp(input, "exit") == 0) break;
        process_input(input);
    }
    return 0;
}