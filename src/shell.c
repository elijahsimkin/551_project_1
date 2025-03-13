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
        perror("execvp failed");
        _exit(1);
    } else if (pid > 0) { // Parent process
        if (!background) {
            waitpid(pid, NULL, 0);
        }
    }
}

void process_input(char *input) {
    int pipe_fd[2], input_fd = 0;
    char *command = NULL, *args[MAX_ARGS];
    int arg_count = 0, background = 0;
    char *commands[10]; // Support for up to 10 piped commands
    int cmd_count = 0;
    
    char *ptr = input;
    while (*ptr) {
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        if (!*ptr) break;
        
        commands[cmd_count++] = ptr;
        while (*ptr && *ptr != '|') ptr++;
        if (*ptr) {
            *ptr++ = '\0';
        }
    }
    
    for (int i = 0; i < cmd_count; i++) {
        char *cmd_ptr = commands[i];
        arg_count = 0;
        while (*cmd_ptr) {
            while (*cmd_ptr == ' ' || *cmd_ptr == '\t') cmd_ptr++;
            if (!*cmd_ptr) break;
            args[arg_count++] = cmd_ptr;
            while (*cmd_ptr && *cmd_ptr != ' ' && *cmd_ptr != '\t') cmd_ptr++;
            if (*cmd_ptr) {
                *cmd_ptr++ = '\0';
            }
        }
        args[arg_count] = NULL;
        
        if (i < cmd_count - 1) {
            pipe(pipe_fd);
            execute_command(args[0], args, input_fd, pipe_fd[PIPE_WRITE], background);
            close(pipe_fd[PIPE_WRITE]);
            input_fd = pipe_fd[PIPE_READ];
        } else {
            execute_command(args[0], args, input_fd, 1, background);
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