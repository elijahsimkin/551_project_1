// day 1 code:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <termios.h> 

#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64
#define MAX_JOBS 16

typedef int bool;
#define true 1
#define false 0

// Global Variables
typedef struct job {
    int id;
    pid_t pid;
    char command[MAX_INPUT_SIZE];
    int status;
} job;

job jobs[MAX_JOBS];
int job_count = 0;

// Function prototypes
void execute_pipe(char *input);
void print_startup_message();
bool instr_not_null(char* instr);
void parse_and_execute(char *input, char *args[]);
void execute_command(char **args, int background, int input_fd, int output_fd);
void handle_redirection(char **args, int *input_fd, int *output_fd);
void add_job(pid_t pid, char *command);
void remove_job(pid_t pid);
void list_jobs();
void bring_to_foreground(int job_id);
void sigchld_handler(int s);

//Startup Message
void print_startup_message() {
    printf("SSS Shell v0.1\n");
    printf("Type 'exit' to exit\n");
}

//Checks if the instruction is not null
bool instr_not_null(char* instr) {
    return *instr != '\0';
}

int main() {
    char input_buffer[MAX_INPUT_SIZE];
    char *args[MAX_ARGS];

    //signal
    signal(SIGCHLD, sigchld_handler);

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

//The redirection function gets the input and output stream from a specific file.
void handle_redirection(char **args, int *input_fd, int *output_fd) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            // Input redirection
            char *filename = args[i + 1];
            if (filename == NULL) {
                fprintf(stderr, "Error: No filename specified for input redirection.\n");
                exit(1);
            }
            *input_fd = open(filename, O_RDONLY);
            if (*input_fd < 0) {
                perror("Error opening input file");
                exit(1);
            }
            args[i] = NULL; // Remove "<" from args
            break;
        } else if (strcmp(args[i], ">") == 0) {
            // Output redirection
            char *filename = args[i + 1];
            if (filename == NULL) {
                fprintf(stderr, "Error: No filename specified for output redirection.\n");
                exit(1);
            }
            *output_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*output_fd < 0) {
                perror("Error opening output file");
                exit(1);
            }
            args[i] = NULL; // Remove ">" from args
            break;
        } else if (strcmp(args[i], ">>") == 0) {
            // Append output redirection
            char *filename = args[i + 1];
            if (filename == NULL) {
                fprintf(stderr, "Error: No filename specified for append output redirection.\n");
                exit(1);
            }
            *output_fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (*output_fd < 0) {
                perror("Error opening append output file");
                exit(1);
            }
            args[i] = NULL; // Remove ">>" from args
            break;
        }
    }
}
//gets the input and separates the command into different parameters
void parse_and_execute(char *input, char *args[]) {
    char *token;
    int i = 0;
    int background = 0;
    char input_copy[MAX_INPUT_SIZE];
    strcpy(input_copy, input);

    // Tokenize the input
    token = strtok(input, " ");
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    args[i] = NULL;

    if (args[0] == NULL) {
        return;
    }

    // Check for background execution
    for (int j = 0; args[j] != NULL; j++) {
        if (strcmp(args[j], "&") == 0) {
            background = 1;
            args[j] = NULL; // Remove "&" from the arguments
            break;
        }
    }
    
    // FIX: Handle `cd` command before calling `execvp()`
    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "cd: missing argument\n");
        } else {
            if (chdir(args[1]) != 0) {  // Change the working directory
                perror("cd failed");
            }
        }
        return; // Prevent `execvp()` from running
    }

    // Handle built-in commands
    if (strcmp(args[0], "exit") == 0) {
        exit(0);
    } else if (strcmp(args[0], "jobs") == 0) {
        list_jobs();
    } else if (strcmp(args[0], "fg") == 0) {
        if (args[1] != NULL) {
            int job_id = atoi(args[1]);
            bring_to_foreground(job_id);
        } else {
            fprintf(stderr, "fg: job number missing\n");
        }
    }else {
        //Redirection code
        int input_fd = STDIN_FILENO, output_fd = STDOUT_FILENO;
        handle_redirection(args, &input_fd, &output_fd);

         // Check for piping
        char *pipe_char = strchr(input_copy, '|');
        if(pipe_char != NULL){
            execute_pipe(input_copy);
        }else{
             execute_command(args, background, input_fd, output_fd);
        }
    }
}

//execute_command function does the forking and calls execvp
void execute_command(char **args, int background, int input_fd, int output_fd) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        setpgid(0, 0); // Create a new process group for the child

        //Handle the redirection
        dup2(input_fd, STDIN_FILENO);
        dup2(output_fd, STDOUT_FILENO);

        // Restore default signal handlers
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        execvp(args[0], args);
        perror("execvp failed");
        exit(1);
    } else if (pid > 0) {
        // Parent process
        if (!background) {
            int status;
            waitpid(pid, &status, WUNTRACED);
        } else {
            add_job(pid, args[0]);
            printf("[%d] %d\n", job_count, pid);
        }
    } else {
        perror("fork failed");
    }
}

//handles the job control commands
void add_job(pid_t pid, char *command) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].id = job_count + 1;
        jobs[job_count].pid = pid;
        strcpy(jobs[job_count].command, command);
        jobs[job_count].status = 0; // Running
        job_count++;
    } else {
        fprintf(stderr, "Too many jobs running in the background.\n");
    }
}
void remove_job(pid_t pid) {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].pid == pid) {
            // Shift the remaining jobs to fill the gap
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

void sigchld_handler(int s) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        remove_job(pid);
    }
}

// Function to execute a pipe command
void execute_pipe(char *input) {
    int pipefd[2];
    pid_t p1, p2;
    char *cmd1 = NULL, *cmd2 = NULL;
    char *saveptr;
    int fd_in = 0, fd_out = 1; //Default file desciptor are stdin and stdout

    char input_copy[MAX_INPUT_SIZE];
    strcpy(input_copy, input);

    // Split the input into two commands based on the pipe symbol
    cmd1 = strtok_r(input_copy, "|", &saveptr);
    if (cmd1 == NULL) {
        fprintf(stderr, "Error: Invalid pipe command.\n");
        return;
    }

    cmd2 = strtok_r(NULL, "|", &saveptr);
    if (cmd2 == NULL) {
        fprintf(stderr, "Error: Invalid pipe command.\n");
        return;
    }

    // Check for input and output redirection in cmd1
    char *args1[MAX_ARGS];
    int i = 0;
    char *token1 = strtok(cmd1, " ");
    while (token1 != NULL && i < MAX_ARGS - 1) {
        if (strcmp(token1, "<") == 0) {
             token1 = strtok(NULL, " ");  // get the input file name
                if (token1 == NULL) {
                     fprintf(stderr, "Missing input file name\n");
                     return;
                }
                if ((fd_in = open(token1, O_RDONLY)) < 0) {
                    perror("Couldn't open input file");
                    return;
                }
                 token1 = strtok(NULL, " ");
        }
        else if (strcmp(token1, ">") == 0){
                 token1 = strtok(NULL, " ");  // get the output file name
                  if (token1 == NULL) {
                     fprintf(stderr, "Missing output file name\n");
                     return;
                 }

                   if ((fd_out = open(token1, O_WRONLY | O_TRUNC | O_CREAT, 0644)) < 0) {
                        perror("Couldn't open output file");
                        return;
                    }
                   token1 = strtok(NULL, " ");
        }
        else{
            args1[i++] = token1;
            token1 = strtok(NULL, " ");
        }
    }
    args1[i] = NULL;
 // Check for input and output redirection in cmd2
    char *args2[MAX_ARGS];
    i = 0;
    char *token2 = strtok(cmd2, " ");

     while (token2 != NULL && i < MAX_ARGS - 1) {
        if (strcmp(token2, "<") == 0) {
             token2 = strtok(NULL, " ");  // get the input file name
                if (token2 == NULL) {
                     fprintf(stderr, "Missing input file name\n");
                     return;
                }
                if ((fd_in = open(token2, O_RDONLY)) < 0) {
                    perror("Couldn't open input file");
                    return;
                }
                 token2 = strtok(NULL, " ");
        }
        else if (strcmp(token2, ">") == 0){
                 token2 = strtok(NULL, " ");  // get the output file name
                  if (token2 == NULL) {
                     fprintf(stderr, "Missing output file name\n");
                     return;
                 }

                   if ((fd_out = open(token2, O_WRONLY | O_TRUNC | O_CREAT, 0644)) < 0) {
                        perror("Couldn't open output file");
                        return;
                    }
                   token2 = strtok(NULL, " ");
        }
        else{
            args2[i++] = token2;
            token2 = strtok(NULL, " ");
        }
    }
    args2[i] = NULL;
//All functions are forking here

    if (pipe(pipefd) < 0) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    p1 = fork();
    if (p1 < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (p1 == 0) {
        // Child 1: Execute the command before the pipe
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        //redirect
         dup2(fd_in, STDIN_FILENO);
        dup2(fd_out, STDOUT_FILENO);
        execvp(args1[0], args1);
        perror("execvp");
        exit(EXIT_FAILURE);

    }

    p2 = fork();
    if (p2 < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (p2 == 0) {
        // Child 2: Execute the command after the pipe
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        dup2(fd_in, STDIN_FILENO);
        dup2(fd_out, STDOUT_FILENO);
        execvp(args2[0], args2);
        perror("execvp");
        exit(EXIT_FAILURE);
    }

    // Parent: Close pipe ends and wait for children
    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(p1, NULL, 0);
    waitpid(p2, NULL, 0);

     if (fd_in != 0) close(fd_in);
    if (fd_out != 1) close(fd_out);
}

void bring_to_foreground(int job_id) {
    if (job_id > 0 && job_id <= job_count) {
        pid_t pid = jobs[job_id - 1].pid;
        int status;

        // (1) Set the terminal's foreground process group to the job's PID
        if (tcsetpgrp(STDIN_FILENO, pid) < 0) {
            perror("tcsetpgrp failed");
            return;
        }

        // (2) Send a SIGCONT signal to resume the process if it's stopped
        kill(-pid, SIGCONT);  // Use negative PID to send to the process group

        // (3) Wait for the process to finish or be stopped
        waitpid(pid, &status, WUNTRACED); //WUNTRACED is important!

        // (4) After it finishes or stops, set the shell back as the foreground process group.
        tcsetpgrp(STDIN_FILENO, getpgrp());

        // (5) Check status and update job if necessary.
        if (WIFSTOPPED(status)) {
            //Job was stopped by a signal (e.g. SIGTSTP)
            for (int i = 0; i < job_count; i++) {
                if (jobs[i].pid == pid) {
                    jobs[i].status = 1; //Stopped.  Define an enum for statuses.
                    break;
                }
            }
        } else {
            //Job terminated normally or was killed
            remove_job(pid);  //Or set the job's status to "completed" or "killed".
        }
    } else {
        fprintf(stderr, "fg: job not found: %d\n", job_id);
    }
}