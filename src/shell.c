#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_ARGS 128
#define PIPE_READ 0
#define PIPE_WRITE 1

void execute_command(char *command, char **args, int input_fd, int output_fd, int background) {
    pid_t pid = fork();
    if (pid == 0) { // Child process
        if (input_fd != 0) {
            dup2(input_fd, 0);
            close(input_fd);
        }
        if (output_fd != 1) {
            dup2(output_fd, 1);
            close(output_fd);
        }
        execvp(command, args);
        _exit(1);
    } else if (pid > 0) { // Parent process
        if (!background) {
            waitpid(pid, NULL, 0);
        }
    }
}

void process_input(char *input) {
    int pipe_fd[2], input_fd = 0;
    char *commands[MAX_ARGS];
    int command_count = 0;
    int background_flags[MAX_ARGS] = {0};

    char *ptr = input;
    while (*ptr) {
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        if (!*ptr) break;
        commands[command_count] = ptr;
        background_flags[command_count] = 0;
        
        while (*ptr && *ptr != '|' && *ptr != '&') ptr++;
        if (*ptr == '|') {
            *ptr++ = '\0';
        } else if (*ptr == '&') {
            *ptr++ = '\0';
            background_flags[command_count] = 1;
        }
        command_count++;
    }
    commands[command_count] = NULL;

    for (int i = 0; i < command_count; i++) {
        char *token, *command = NULL, *args[MAX_ARGS];
        int arg_count = 0;
        
        char *cmd_ptr = commands[i];
        while (*cmd_ptr) {
            while (*cmd_ptr == ' ' || *cmd_ptr == '\t') cmd_ptr++;
            if (!*cmd_ptr) break;
            token = cmd_ptr;
            while (*cmd_ptr && *cmd_ptr != ' ' && *cmd_ptr != '\t' && *cmd_ptr != '>') cmd_ptr++;
            if (*cmd_ptr) {
                *cmd_ptr++ = '\0';
                while (*cmd_ptr == ' ' || *cmd_ptr == '\t') cmd_ptr++;
            }
            
            if (*token == '\0') continue;
            
            if (strcmp(token, ">") == 0) {
                token = cmd_ptr;
                while (*cmd_ptr && *cmd_ptr != ' ' && *cmd_ptr != '\t') cmd_ptr++;
                if (*cmd_ptr) {
                    *cmd_ptr++ = '\0';
                    while (*cmd_ptr == ' ' || *cmd_ptr == '\t') cmd_ptr++;
                }
                int fd = open(token, 577, 0644); // O_WRONLY | O_CREAT | O_TRUNC
                if (fd < 0) {
                    return;
                }
                execute_command(command, args, input_fd, fd, background_flags[i]);
                close(fd);
                input_fd = 0;
                arg_count = 0;
            } else {
                if (arg_count == 0) command = token;
                args[arg_count++] = token;
                args[arg_count] = NULL;
            }
        }
        
        if (i < command_count - 1) {
            pipe(pipe_fd);
            execute_command(command, args, input_fd, pipe_fd[PIPE_WRITE], background_flags[i]);
            close(pipe_fd[PIPE_WRITE]);
            input_fd = pipe_fd[PIPE_READ];
        } else {
            execute_command(command, args, input_fd, 1, background_flags[i]);
        }
    }
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