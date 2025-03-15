#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>

#define MAX_ARGS 128
#define MAX_JOBS 64
#define PIPE_READ 0
#define PIPE_WRITE 1
#define MAX_HISTORY 100
#define MAX_INPUT 1024 /* Limit input to 1 KB */

typedef struct {
    int job_id;
    pid_t pid;
    char command[256];
    int active;
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;

char history[MAX_HISTORY][1024];
int history_index = 0;
int history_pos = -1;

/* Global flag to track if a foreground process is running */
pid_t foreground_pid = -1;

void sigint_handler(int sig) {
    if (foreground_pid > 0) {
        /* If a foreground process is running, send SIGINT to it */
        kill(foreground_pid, SIGINT);
    } else {
        /* Print a new prompt instead of terminating the shell */
        printf("\n");
        printf("shell> ");
        fflush(stdout);
    }
}

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
    int i;
    for (i = 0; i < job_count; i++) {
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
    int i;
    for (i = 0; i < job_count; i++) {
        if (jobs[i].active) {
            printf("[Job %d] Running: %s (PID: %d)\n", jobs[i].job_id, jobs[i].command, jobs[i].pid);
        }
    }
}

void foreground_job(int job_id) {
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

/* Function to handle cd */
void handle_cd(char **args) {
    if (args[1] == NULL) {
        /* Change to home directory if no argument is provided */
        const char *home = getenv("HOME");
        if (home == NULL) home = "/";  /* Fallback to root if HOME is not set */
        if (chdir(home) != 0) {
            perror("cd failed");
        }
    } else {
        /* Change to specified directory */
        if (chdir(args[1]) != 0) {
            perror("cd failed");
        }
    }
}

void execute_command(char *command, char **args, int input_fd, int output_fd, int background) {
    pid_t pid = fork();
    if (pid == 0) { /* Child process */
        if (input_fd != STDIN_FILENO) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (output_fd != STDOUT_FILENO) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }

        /* Restore default Ctrl+C behavior for child processes */
        signal(SIGINT, SIG_DFL);

        execvp(command, args);
        perror("execvp failed");
        _exit(1);
    } else if (pid > 0) { /* Parent process */
        if (input_fd != STDIN_FILENO) close(input_fd);
        if (output_fd != STDOUT_FILENO) close(output_fd);
        
        if (!background) {
            /* Track foreground process */
            foreground_pid = pid;

            /* Wait for foreground process to finish */
            waitpid(pid, NULL, 0);

            /* Reset foreground process tracking */
            foreground_pid = -1;
        } else {
            add_job(pid, command);
            printf("[Job %d] Started: %s (PID: %d)\n", job_count, command, pid);
        }
    }
}

void process_input(char *input) {
    check_jobs();

    /* Check if input exceeds 1024 characters */
    if (strlen(input) >= MAX_INPUT - 1) {
        printf("Error: Input exceeds 1024 characters! Command ignored.\n");
        return;
    }

    /* Ignore empty inputs */
    if (strlen(input) == 0) return;

    if (strncmp(input, "jobs", 4) == 0) {
        list_jobs();
        return;
    }
    if (strncmp(input, "fg", 2) == 0) {
        int job_id = atoi(input + 3);
        foreground_job(job_id);
        return;
    }

    /* Add to history */
    strncpy(history[history_index % MAX_HISTORY], input, sizeof(history[history_index % MAX_HISTORY]) - 1);
    history_index++;
    history_pos = history_index;
    
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
    
    int i;
    for (i = 0; i < command_count; i++) {
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

        /* Handle cd command */
        if (strcmp(args[0], "cd") == 0) {
            handle_cd(args);
            return;
        }
        
        if (i < command_count - 1) {
            pipe(pipe_fd);
            execute_command(command, args, input_fd, pipe_fd[PIPE_WRITE], background_flags[i]);
            close(pipe_fd[PIPE_WRITE]); /* Close write end in parent */
            input_fd = pipe_fd[PIPE_READ]; /* Pass read end to next command */
        } else {
            execute_command(command, args, input_fd, output_fd, background_flags[i]);
            if (input_fd != STDIN_FILENO) close(input_fd); /* Close last input_fd */
        }
    }
    if (output_fd != STDOUT_FILENO) close(output_fd);
}

void enable_raw_mode() {
    struct termios term_settings;

    tcgetattr(STDIN_FILENO, &term_settings);

    term_settings.c_lflag &= ~(ICANON | ECHO);

    tcsetattr(STDIN_FILENO, TCSANOW, &term_settings);
}

void disable_raw_mode() {
    struct termios term_settings;

    tcgetattr(STDIN_FILENO, &term_settings);

    term_settings.c_lflag |= ICANON | ECHO;

    tcsetattr(STDIN_FILENO, TCSANOW, &term_settings);
}

int main() {
    char input[MAX_INPUT + 1];
    enable_raw_mode(); /* Enable raw mode for handling arrow keys */

    signal(SIGINT, sigint_handler); /* Register Ctrl+C handler */

    while (1) {
        printf("shell> ");
        fflush(stdout);

        int pos = 0;
        memset(input, 0, sizeof(input));

        while (1) {
            char c = getchar();

            if (c == '\n') { /* Enter key */
                input[pos] = '\0';
                break;
            } else if (c == EOF) { 
                printf("\n");
                exit(0); /* Handle Ctrl+D (EOF) */
            } else if (pos >= MAX_INPUT) {
                /* If input exceeds limit, flush excess characters */
                while (getchar() != '\n'); /* Discard rest of line */
                printf("\nError: Input exceeds %d characters! Command ignored.\n", MAX_INPUT);
                continue; /* Skip `process_input()` */
            } else if (c == '\x1B') { /* Escape sequence for arrow keys */
                getchar(); /* Skip '[' */
                char arrow = getchar();
                if (arrow == 'A') { /* Up arrow */
                    if (history_pos > 0) {
                        history_pos--;
                        strcpy(input, history[history_pos % MAX_HISTORY]);
                        printf("\r\033[Kshell> %s", input); /* Clear line and print history */
                        fflush(stdout);
                        pos = strlen(input);
                    }
                } else if (arrow == 'B') { /* Down arrow */
                    if (history_pos < history_index - 1) {
                        history_pos++;
                        strcpy(input, history[history_pos % MAX_HISTORY]);
                        printf("\r\033[Kshell> %s", input); /* Clear line and print history */
                        fflush(stdout);
                        pos = strlen(input);
                    } else {
                        history_pos = history_index; /* Reset to end of history */
                        memset(input, 0, sizeof(input));
                        printf("\r\033[Kshell> "); /* Clear line */
                        fflush(stdout);
                        pos = 0;
                    }
                } else if (arrow == 'D') { /* Left arrow */
                    if (pos > 0) {
                        pos--; /* Move cursor left in the buffer */
                        printf("\b"); /* Move cursor left on screen */
                        fflush(stdout);
                    }
                } else if (arrow == 'C') { /* Right arrow */
                    if (pos < strlen(input)) {
                        printf("%c", input[pos]); /* Move cursor right on screen */
                        fflush(stdout);
                        pos++; /* Move cursor right in the buffer */
                    }
                }
            } else if (c == '\x7F') { /* Backspace key */
                if (pos > 0) {
                    pos--;
                    input[pos] = '\0';
                    printf("\b \b"); /* Erase character from terminal */
                    fflush(stdout);
                }
            } else { /* Regular character input */
                input[pos++] = c;
                putchar(c);
                fflush(stdout);
            }
        }

        if (pos == 0) continue; /* Ignore empty input */
        if (strcmp(input, "exit") == 0) break; /* Exit command */

        printf("\n"); /* Add a newline before processing the command output */
        process_input(input); /* Process the user input */
    }

    disable_raw_mode(); /* Restore terminal settings before exiting */
    return 0;
}
