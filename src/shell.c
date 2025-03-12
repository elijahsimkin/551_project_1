#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h> //Required for file control option

#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64
#define MAX_JOBS 16

typedef int bool;
#define true 1
#define false 0

//Global Variables
typedef struct job {
    int id;
    pid_t pid;
    char command[MAX_INPUT_SIZE];
    int status; //0 running, 1 stopped, 2 killed, 3 completed
} job;

job jobs[MAX_JOBS];
int job_count = 0;

// Function prototypes
void print_startup_message();
bool instr_not_null(char* instr);
void parse_and_execute(char *input, char *args[]);
void execute_command(char **args, int background);
void handle_redirection(char **args, int *input_fd, int *output_fd, int *background);
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
    int status;

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

    int input_fd = STDIN_FILENO, output_fd = STDOUT_FILENO;
    handle_redirection(args, &input_fd, &output_fd, &background);

    // Execute the command
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
    } else {
        execute_command(args, background);
    }
}

//execute_command function does the forking and calls execvp
void execute_command(char **args, int background) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        setpgid(0, 0); // Create a new process group for the child

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

//The redirection function gets the input and output stream from a specific file.
void handle_redirection(char **args, int *input_fd, int *output_fd, int *background) {
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
        }
    }
    //After we handle redirection, we do another fork-execute
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
void bring_to_foreground(int job_id) {
    if (job_id > 0 && job_id <= job_count) {
        pid_t pid = jobs[job_id - 1].pid;
        //Bring to foreground
    }
}
void sigchld_handler(int s) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        remove_job(pid);
    }
}