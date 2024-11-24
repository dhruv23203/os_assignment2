
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

// for input purposes
#define MAX_CMD_LEN 1024
#define MAX_ARGS 100
#define HISTORY_LIMIT 100
#define MAX_JOBS 50

// Structure for command history
typedef struct {
    char *entries[HISTORY_LIMIT];
    int size;
} History;

typedef struct {
    pid_t pid;
    char *cmd;
    time_t start_time;  // Add start_time to track when the job started
} Job;

History shell_history = {.size = 0};
Job jobs[MAX_JOBS];
int job_count = 0;

// Function to display the shell prompt
void show_prompt() {
    printf("MyShell> ");
}

// Function to add a command to the history
void save_to_history(const char *cmd) {
    if (shell_history.size < HISTORY_LIMIT) {
        shell_history.entries[shell_history.size] = strdup(cmd);
        shell_history.size++;
    }
    else {
        free(shell_history.entries[0]);
        for (int i = 1; i < HISTORY_LIMIT; i++) {
            shell_history.entries[i - 1] = shell_history.entries[i];
        }
        shell_history.entries[HISTORY_LIMIT - 1] = strdup(cmd);
    }
}

// Function to print the command history
void show_history() {
    int i = 0;
    while (i < shell_history.size) {
        printf("%d: %s", i + 1, shell_history.entries[i]);
        i++;
    }
}


// Function to add a job (background process) to the job list
void add_job(pid_t pid, const char *cmd,time_t start_time) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].pid = pid;
        jobs[job_count].cmd = strdup(cmd);
        jobs[job_count].start_time = start_time;
        job_count++;
    }
}

// Function to list background jobs
void list_jobs() {
    printf("Background Jobs:\n");
    int i = 0;
    while (i < job_count) {
        printf("[%d] PID: %d - %s\n (Started at: %s)\n", i + 1, jobs[i].pid, jobs[i].cmd,ctime(&jobs[i].start_time));
        i++;
    }
}

void handle_signal(int signal) {
    if (signal == SIGINT) {
        printf("\n Caught {Ctrl+C}, Type 'exit' to quit.\n");
        show_prompt();
        fflush(stdout);
    }
    else if (signal == SIGTSTP) {
        printf("\n Caught {Ctrl+Z} \n");
        show_prompt();
        fflush(stdout);
    }
}

// Function to split a command string into an array of arguments
char **split_command(const char *cmd, int *bg_process) {
    char **arguments = malloc(MAX_ARGS * sizeof(char *));
    char *cmd_copy = strdup(cmd);
    char *token;
    int idx = 0;

    token = strtok(cmd_copy, " \t\n");
    while (token != NULL) {
        arguments[idx++] = strdup(token);
        token = strtok(NULL, " \t\n");
    }
    arguments[idx] = NULL;

    // for background processes
    if (idx > 0 && strcmp(arguments[idx - 1], "&") == 0) {
        *bg_process = 1;
        free(arguments[--idx]);
        arguments[idx] = NULL;
    } else {
        *bg_process = 0;
    }

    free(cmd_copy);
    return arguments;
}

// Function to handle input/output redirection
void handle_redirection(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0) {
            int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("Unable to open output file");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            args[i] = NULL;
        }
        else if (strcmp(args[i], "<") == 0) {
            int fd = open(args[i + 1], O_RDONLY);
            if (fd < 0) {
                perror("Unable to open input file");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
            args[i] = NULL;
        }
    }
}

void execute_piped_commands(char *cmd) {
    char *commands[MAX_ARGS];
    int i = 0;

    // Split the command into multiple commands by pipe |
    commands[i] = strtok(cmd, "|");
    while (commands[i] != NULL) {
        commands[++i] = strtok(NULL, "|");
    }

    int num_cmds = i;
    int pipes[num_cmds - 1][2];  // Pipe array to connect commands

    time_t start_time = time(NULL);  // Capture the start time for piped commands

    for (int j = 0; j < num_cmds; j++) {
        // Create a pipe for all but the last command
        if (j < num_cmds - 1) {
            if (pipe(pipes[j]) == -1) {
                perror("Pipe creation failed");
                exit(EXIT_FAILURE);
            }
        }

        // Fork process for each command
        pid_t pid = fork();
        if (pid == 0) {
            // Child process: set up input/output for pipes

            // If it's not the first command, get input from the previous pipe
            if (j > 0) {
                dup2(pipes[j - 1][0], STDIN_FILENO);
                close(pipes[j - 1][0]);
            }

            // If it's not the last command, redirect output to the next pipe
            if (j < num_cmds - 1) {
                dup2(pipes[j][1], STDOUT_FILENO);
                close(pipes[j][1]);
            }

            // Close all pipes
            for (int k = 0; k < num_cmds - 1; k++) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }

            // Split the command into arguments and execute it
            int bg_process;
            char **args = split_command(commands[j], &bg_process);
            handle_redirection(args);
            execvp(args[0], args);
            perror("Command execution failed");
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("Fork failed");
        }
    }

    // Close all pipes in the parent process
    for (int k = 0; k < num_cmds - 1; k++) {
        close(pipes[k][0]);
        close(pipes[k][1]);
    }

    // Wait for all child processes to finish
    for (int j = 0; j < num_cmds; j++) {
        wait(NULL);
    }

    // Capture end time and calculate execution time
    time_t end_time = time(NULL);
    double exec_time = difftime(end_time, start_time);

    // Print start time and execution time for the piped command sequence
    printf("Piped command started at: %s", ctime(&start_time));
    printf("Total execution time: %.2f seconds\n", exec_time);
}

// Function to execute non-piped commands
void run_command(char **args, int bg_process, const char *cmd) {
    pid_t pid;
    time_t start_time = time(NULL);  // Capture the start time

    if ((pid = fork()) == 0) {
        // In child process
        handle_redirection(args);
        execvp(args[0], args);  // Execute the command
        perror("Execution failed");
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("Fork failed");
    } else {
        // In parent process
        if (bg_process) {
            printf("Running in background (PID: %d)\n", pid);
            add_job(pid, cmd, start_time);
        } else {
            int status;
            waitpid(pid, &status, 0);  // Wait for the child process to finish

            // Capture end time and calculate execution time
            time_t end_time = time(NULL);
            double exec_time = difftime(end_time, start_time);

            // Print process ID and execution time
            printf("Command Start time: %s", ctime(&start_time));
            printf("Command executed by PID: %d\n", pid);
            printf("Execution time: %.2f seconds\n", exec_time);
        }
    }
}
// Main loop
int main() {
    signal(SIGINT, handle_signal);  // Handle Ctrl+C
    signal(SIGTSTP, handle_signal); // Handle Ctrl+Z

    char cmd[MAX_CMD_LEN];

    while (1) {
        show_prompt();

        if (fgets(cmd, MAX_CMD_LEN, stdin) == NULL) {
            break;  // Handle end of input (Ctrl+D)
        }

        // Skip empty commands
        if (strcmp(cmd, "\n") == 0) {
            continue;
        }

        // Store the command in history
        save_to_history(cmd);

        // Handle exit command
        if (strncmp(cmd, "exit", 4) == 0) {
            printf("Exiting MyShell...\n");
            break;
        }

        // Handle history command
        if (strncmp(cmd, "history", 7) == 0) {
            show_history();
            continue;
        }

        // Handle jobs command
        if (strncmp(cmd, "jobs", 4) == 0) {
            list_jobs();
            continue;
        }

        // Handle piped commands
        if (strchr(cmd, '|') != NULL) {
            execute_piped_commands(cmd);
            continue;
        }

        // Parse and execute the command
        int bg_process;
        char **args = split_command(cmd, &bg_process);
        if (args[0] != NULL) {
            run_command(args, bg_process, cmd);
        }

        // Free dynamically allocated memory for args
        for (int i = 0; args[i] != NULL; i++) {
            free(args[i]);
        }
        free(args);
    }

    // Free history memory after shell exits
    for (int i = 0; i < shell_history.size; i++) {
        free(shell_history.entries[i]);
    }

    // Free jobs memory
    for (int i = 0; i < job_count; i++) {
        free(jobs[i].cmd);
    }

    return 0;
}
