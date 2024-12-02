#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>

#define _stdin 0

// Token types
typedef enum {
    TOK_WORD,     // Regular word/argument
    TOK_REDIRECT_IN,  // < input redirection
    TOK_REDIRECT_OUT, // > output redirection
    TOK_PIPE,     // | pipe
    TOK_NEWLINE,  // Newline
    TOK_END       // End of stream
} tok_type;

// Dynamic string buffer
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} dyn_string;

// Dynamic array list
typedef struct {
    char **val;
    size_t size;
    size_t capacity;
} arraylist_t;

// Command structure
typedef struct command {
    arraylist_t *args;
    char *execpath;
    char *inputfile;
    char *outputfile;
    struct command *next_command;  // Pointer to next command in a pipe chain
} command;

// Custom strdup implementation because the built in one doesnt work
char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

// Function prototypes
void free_command(command *c);
void free_arrlist(arraylist_t *al);
int search_exec(command *c);
void launcher(command *c, int is_interactive);

// Initialize dynamic string
void init_dyn_string(dyn_string *str) {
    str->data = malloc(10);
    if (!str->data) {
        perror("mysh: Failed to allocate dynamic string");
        exit(EXIT_FAILURE);
    }
    str->size = 0;
    str->capacity = 10;
    str->data[0] = '\0';
}

// Ensure capacity for dynamic string
void ensure_dyn_string_capacity(dyn_string *str, size_t needed_size) {
    if (str->size + needed_size >= str->capacity) {
        while (str->size + needed_size >= str->capacity) {
            str->capacity *= 2;
        }
        char *new_data = realloc(str->data, str->capacity);
        if (!new_data) {
            perror("mysh: Failed to resize dynamic string");
            exit(EXIT_FAILURE);
        }
        str->data = new_data;
    }
}

// Append to dynamic string
void append_dyn_string(dyn_string *str, char c) {
    ensure_dyn_string_capacity(str, 2);  // 1 for char, 1 for terminator
    str->data[str->size++] = c;
    str->data[str->size] = '\0';
}

// Free dynamic string structure
void free_dyn_string(dyn_string *str) {
    free(str->data);
    str->data = NULL;
    str->size = 0;
    str->capacity = 0;
}

// Allocate and initialize arraylist
void init_arrlist(arraylist_t *al) {
    al->size = 0;
    al->capacity = 10;  
    al->val = malloc(al->capacity * sizeof(char *));
    if (al->val == NULL) {
        exit(EXIT_FAILURE);
    }
}

// Push item to arraylist
void arrlist_push(arraylist_t *al, char *item) {
    if (al->size == al->capacity) {
        al->capacity *= 2;
        char **new_val = realloc(al->val, al->capacity * sizeof(char *));
        if (new_val == NULL) {
            // Freeing memory and exit
            for (size_t i = 0; i < al->size; i++) {
                free(al->val[i]);
            }
            free(al->val);
            exit(EXIT_FAILURE);
        }
        al->val = new_val;
    }
    al->val[al->size] = strdup(item);
    if (al->val[al->size] == NULL) {
        exit(EXIT_FAILURE);
    }
    al->size++;
}

// Free arraylist
void free_arrlist(arraylist_t *al) {
    if (al == NULL) return;

    if (al->val) {
        for (size_t i = 0; i < al->size; i++) {
            if (al->val[i]) {
                free(al->val[i]);
                al->val[i] = NULL;
            }
        }
        free(al->val);
        al->val = NULL;
    }
}

// Free command structure with piping support
void free_command(command *c) {
    if (c == NULL) return;

    // Free the entire commands linked list
    while (c != NULL) {
        command *next = c->next_command;

        if (c->execpath) {
            free(c->execpath);
            c->execpath = NULL;
        }
        if (c->inputfile) {
            free(c->inputfile);
            c->inputfile = NULL;
        }
        if (c->outputfile) {
            free(c->outputfile);
            c->outputfile = NULL;
        }
        if (c->args) {
            free_arrlist(c->args);
            free(c->args);
            c->args = NULL;
        }

        free(c);
        c = next;
    }
}


// Search for executable in allowed directories
int search_exec(command *c) {
    const char *allowed_dirs[] = {"/usr/local/bin", "/usr/bin", "/bin"};
    size_t num_dirs = sizeof(allowed_dirs) / sizeof(allowed_dirs[0]);

    char *cmd = c->execpath;

    // Check if command contains a slash
    if (strchr(cmd, '/') != NULL) {
        // Treat as a  path
        if (access(cmd, F_OK) == 0) {
            return 0;
        } else {
            return 1;
        }
    }

    // Search in allowed directories
    char *token_path = NULL;
    for (size_t i = 0; i < num_dirs; i++) {
        size_t size_needed = strlen(allowed_dirs[i]) + strlen(cmd) + 2;
        token_path = realloc(token_path, size_needed);
        if (token_path == NULL) {
            perror("mysh: realloc failed");
            exit(EXIT_FAILURE);
        }

        snprintf(token_path, size_needed, "%s/%s", allowed_dirs[i], cmd);
        if (access(token_path, F_OK) == 0) {
            free(c->execpath);
            c->execpath = strdup(token_path);
            free(token_path);
            return 0;
        }
    }

    free(token_path);
    return 1;
}

void execute_piped_commands(command *first_cmd, int is_interactive) {
    int pipes[2][2];  // Two sets of pipe file descriptors
    int in_fd = STDIN_FILENO;
    command *current_cmd = first_cmd;
    int pipe_index = 0;
    pid_t *child_pids = NULL;
    int num_commands = 0;
    
    // Count number of commands in pipeline
    command *cmd_counter = first_cmd;
    while (cmd_counter != NULL) {
        num_commands++;
        cmd_counter = cmd_counter->next_command;
    }
    
    // Allocate array for child pids
    child_pids = malloc(num_commands * sizeof(pid_t));
    if (!child_pids) {
        perror("mysh: Failed to allocate PID array");
        return;
    }
    
    int cmd_index = 0;

    while (current_cmd != NULL) {
        // Perform executable search
        if (search_exec(current_cmd) != 0) {
            fprintf(stderr, "mysh: Command not found: %s\n", current_cmd->execpath);
            // Clean up previously created processes
            for (int i = 0; i < cmd_index; i++) {
                kill(child_pids[i], SIGTERM);
                waitpid(child_pids[i], NULL, 0);
            }
            free(child_pids);
            return;
        }

        // Create pipe for next command, except for the last command
        if (current_cmd->next_command != NULL) {
            if (pipe(pipes[pipe_index]) < 0) {
                perror("mysh: Pipe creation failed");
                // Clean up previously created processes
                for (int i = 0; i < cmd_index; i++) {
                    kill(child_pids[i], SIGTERM);
                    waitpid(child_pids[i], NULL, 0);
                }
                free(child_pids);
                return;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("mysh: Fork failed");
            // Clean up previously created processes to clean up
            for (int i = 0; i < cmd_index; i++) {
                kill(child_pids[i], SIGTERM);
                waitpid(child_pids[i], NULL, 0);
            }
            free(child_pids);
            return;
        } else if (pid == 0) {  // Child process
            // Free the pid array in child process
            free(child_pids);
            
            // Handle input redirection or previous pipe's output
            if (in_fd != STDIN_FILENO) {
                if (dup2(in_fd, STDIN_FILENO) < 0) {
                    perror("mysh: dup2 input failed");
                    exit(EXIT_FAILURE);
                }
                close(in_fd);
            }

            // Handle input file redirection for first command
            if (current_cmd == first_cmd && current_cmd->inputfile) {
                int input_fd = open(current_cmd->inputfile, O_RDONLY);
                if (input_fd < 0) {
                    perror("mysh: Input file open failed");
                    exit(EXIT_FAILURE);
                }
                if (dup2(input_fd, STDIN_FILENO) < 0) {
                    perror("mysh: dup2 for input redirection failed");
                    close(input_fd);
                    exit(EXIT_FAILURE);
                }
                close(input_fd);
            }

            // Handle output
            if (current_cmd->next_command != NULL) {
                if (dup2(pipes[pipe_index][1], STDOUT_FILENO) < 0) {
                    perror("mysh: dup2 output failed");
                    exit(EXIT_FAILURE);
                }
            } else if (current_cmd->outputfile) {  // Last command with redirection
                int output_fd = open(current_cmd->outputfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP, 0640);
                if (output_fd < 0) {
                    perror("mysh: Output file open failed");
                    exit(EXIT_FAILURE);
                }
                if (dup2(output_fd, STDOUT_FILENO) < 0) {
                    perror("mysh: dup2 for output redirection failed");
                    close(output_fd);
                    exit(EXIT_FAILURE);
                }
                close(output_fd);
            }

            // Close all pipe fds in child
            if (current_cmd->next_command != NULL) {
                close(pipes[pipe_index][0]);
                close(pipes[pipe_index][1]);
            }
            if (in_fd != STDIN_FILENO) {
                close(in_fd);
            }

            // Execute the command
            char **argv = malloc((current_cmd->args->size + 2) * sizeof(char *));
            if (!argv) {
                perror("mysh: Failed to allocate argv");
                exit(EXIT_FAILURE);
            }
            argv[0] = current_cmd->execpath;
            for (size_t i = 0; i < current_cmd->args->size; i++) {
                argv[i + 1] = current_cmd->args->val[i];
            }
            argv[current_cmd->args->size + 1] = NULL;

            execv(current_cmd->execpath, argv);
            perror("mysh: Exec failed");
            free(argv);
            exit(EXIT_FAILURE);
        } else {  // Parent process
            child_pids[cmd_index++] = pid;
            
            // Close unused pipe ends
            if (current_cmd->next_command != NULL) {
                close(pipes[pipe_index][1]);
                in_fd = pipes[pipe_index][0];
                pipe_index = 1 - pipe_index;  // Alternate between 0 and 1 for the pipes, this was experimental
            }
        }

        current_cmd = current_cmd->next_command;
    }

    // Parent waits for all children to complete in order to move on
    for (int i = 0; i < num_commands; i++) {
        int status;
        waitpid(child_pids[i], &status, 0);
        
        // Print status for the last command in the pipeline and only in interactive mode
        if (i == num_commands - 1 && is_interactive) {
            if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                if (exit_code != 0) {
                    fprintf(stderr, "mysh: Command Failed: code %d\n", exit_code);
                }
            } else if (WIFSIGNALED(status)) {
                fprintf(stderr, "mysh: Terminated by signal: ");
                psignal(WTERMSIG(status), NULL);
            }
        }
    }

    free(child_pids);

    // Close any remaining files
    if (in_fd != STDIN_FILENO) {
        close(in_fd);
    }
}

void execute_external_command(command *c, int is_interactive) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("mysh: Fork failed");
        return;
    } else if (pid == 0) {  // Child process
        // Handle input redirection
        if (c->inputfile) {
            int input_fd = open(c->inputfile, O_RDONLY);
            if (input_fd < 0) {
                perror("mysh: Input file open failed");
                exit(EXIT_FAILURE);
            }
            if (dup2(input_fd, STDIN_FILENO) < 0) {
                close(input_fd);
                perror("mysh: dup2 for input redirection failed");
                exit(EXIT_FAILURE);
            }
            close(input_fd);
        }

        // Handle output redirection
        if (c->outputfile) {
            int output_fd = open(c->outputfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP, 0640);
            if (output_fd < 0) {
                perror("mysh: Output file open failed");
                exit(EXIT_FAILURE);
            }
            if (dup2(output_fd, STDOUT_FILENO) < 0) {
                close(output_fd);
                perror("mysh: dup2 for output redirection failed");
                exit(EXIT_FAILURE);
            }
            close(output_fd);
        }

        // Prepare arguments for execv
        char **argv = malloc((c->args->size + 2) * sizeof(char *));
        if (!argv) {
            perror("mysh: Memory allocation failed");
            exit(EXIT_FAILURE);
        }

        int err = search_exec(c);
        if (err != 0) {
            free(argv);
            fprintf(stderr, "mysh: Command not found: %s\n", c->execpath);
            exit(127);
        }

        argv[0] = c->execpath;
        for (size_t i = 0; i < c->args->size; i++) {
            argv[i + 1] = c->args->val[i];
        }
        argv[c->args->size + 1] = NULL;

        execv(c->execpath, argv);
        
        // If execv fails
        free(argv);
        perror("mysh: Command execution failed");
        exit(EXIT_FAILURE);
    } 
    
    // Parent process
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "mysh: Terminated by signal: ");
        psignal(WTERMSIG(status), NULL);
    } else if (is_interactive && WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code != 0) {
            fprintf(stderr, "mysh: Command Failed: code %d\n", exit_code);
        }
    }
}

// Built-in command implementations

//Built in exit
void myexit(command *c) {
    for (int i = 0; i < c->args->size; i++) {
        printf("%s ", c->args->val[i]);
    }

    printf("\nExiting shell...\n");
    free_command(c);
    exit(EXIT_SUCCESS);
}

// Built in which
void mywhich(command *c) {
    // Ensure there is exactly one argument in args
    if (c->args->size != 1) {
        fprintf(stderr, "mysh: Improper usage of which: which <command>\n");
        return;
    }

    const char *allowed_dirs[] = {"/usr/local/bin", "/usr/bin", "/bin"};
    size_t num_dirs = sizeof(allowed_dirs) / sizeof(allowed_dirs[0]);

    char *cmd = c->args->val[0];

    // Check if command contains a slash (explicit path)
    if (strchr(cmd, '/') != NULL) {
        // Treat as an explicit path
        if (access(cmd, F_OK) == 0) {
            printf("%s\n", cmd);
            return;
        } else {
            return;
        }
    }

    // Search in allowed directories
    char *token_path = NULL;
    for (size_t i = 0; i < num_dirs; ++i) {
        size_t size_needed = strlen(allowed_dirs[i]) + strlen(cmd) + 2;
        token_path = realloc(token_path, size_needed);
        if (token_path == NULL) {
            perror("mysh: realloc failed");
            exit(EXIT_FAILURE);
        }

        snprintf(token_path, size_needed, "%s/%s", allowed_dirs[i], cmd);
        if (access(token_path, F_OK) == 0) {
            printf("%s\n", token_path);
            free(token_path);
            return;
        }
    }

    free(token_path);
    return;
}

// Built in pwd command
void mypwd(command *c) {

    if (c->args->size != 0) {
        fprintf(stderr, "mysh: Improper usage of pwd: pwd <no args permitted>\n");
    }

    char *cwd = getcwd(NULL, 0);
    if (cwd != NULL) {
        printf("%s\n", cwd);
        free(cwd);
    } else {
        fprintf(stderr, "pwd: getcwd error");
        return;
    }
}
// Built in cd command
void mycd(command *c) {
    if (c->args->size != 1) {
        fprintf(stderr, "mysh: Improper usage of cd: cd <directory>\n");
        return;
    }

    char *dir = c->args->val[0];

    if (chdir(dir) == 0) {
        char *cwd = getcwd(NULL, 0);
        if (cwd != NULL) {
            free(cwd);
        } else {
            fprintf(stderr, "cd: Error retrieving the current working directory");
        }
    } else {
        fprintf(stderr,"cd: %s\n", strerror(errno));
    }
}

// Token parsing function
tok_type next_token(char **cursor, dyn_string *token_value) {
    char *start = *cursor;
    
    // Reset dynamic string
    token_value->size = 0;
    token_value->data[0] = '\0';
    
    // Skip leading whitespace
    while (*start && (*start == ' ' || *start == '\t')) {
        start++;
    }
    
    // End of string or just whitespace
    if (*start == '\0' || *start == '\n') {
        *cursor = start;
        return TOK_END;
    }
    
    // Redirection and pipe
    if (*start == '<') {
        *cursor = start + 1;
        return TOK_REDIRECT_IN;
    }
    if (*start == '>') {
        *cursor = start + 1;
        return TOK_REDIRECT_OUT;
    }
    if (*start == '|') {
        *cursor = start + 1;
        return TOK_PIPE;
    }
    
    // Word/argument parsing for tokenizing
    while (*start && 
           *start != ' ' && 
           *start != '\t' && 
           *start != '<' && 
           *start != '>' && 
           *start != '|' && 
           *start != '\n') {
        append_dyn_string(token_value, *start);
        start++;
    }
    
    if (token_value->size > 0) {
        *cursor = start;
        return TOK_WORD;
    }
    
    // Unexpected case to cover my ass
    *cursor = start;
    return TOK_END;
}

// Modified launcher function
void launcher(command *c, int is_interactive) {
    // Check if this is a piped command sequence
    if (c->next_command != NULL) {
        execute_piped_commands(c, is_interactive);
        return;
    }

    // Built-in commands
    if (strcmp(c->execpath, "exit") == 0) {
            myexit(c);
            free_command(c);
            exit(EXIT_SUCCESS);
    }
    else if (strcmp(c->execpath, "which") == 0) {
        mywhich(c);
        return;
    }
    else if (strcmp(c->execpath, "pwd") == 0) {
        mypwd(c);
        return;
    }
    else if (strcmp(c->execpath, "cd") == 0) {
        mycd(c);
        return;
    }
    // execute externally for any other command
    else {
        execute_external_command(c, is_interactive);
    }
}

void parse_input(int fd, int is_interactive) {
    int bufsize = 1;  // Start with small initial size
    int total_bytes_read = 0;
    char *buf = malloc(bufsize);
    if (buf == NULL) {
        perror("mysh: Buffer allocation failed");
        return;
    }

    // Dynamic buffer reading
    while (1) {
        if (total_bytes_read >= bufsize - 1) {
            bufsize *= 2;
            char *new_buf = realloc(buf, bufsize);
            if (new_buf == NULL) {
                perror("mysh: Buffer reallocation failed");
                free(buf);
                return;
            }
            buf = new_buf;
        }

        // Read into the remaining buffer space
        int bytes_read = read(fd, buf + total_bytes_read, 1);
        
        if (bytes_read <= 0) {
            // EOF or error
            if (bytes_read < 0) {
                perror("mysh: Read error");
            } else if (bytes_read == 0 && total_bytes_read == 0) {
                free(buf);
                exit(EXIT_SUCCESS);  // Exit on EOF 
            }
            break;  // Break if we have partial data to process
        }

        total_bytes_read++;
        buf[total_bytes_read] = '\0';

        // Check if we have read a full line
        if (buf[total_bytes_read - 1] == '\n') {
            break;
        }
    }

    // Remove trailing newline in the command
    if (total_bytes_read > 0 && buf[total_bytes_read - 1] == '\n') {
        buf[total_bytes_read - 1] = '\0';
        total_bytes_read--;
    }

    // Skip completely empty input
    if (total_bytes_read == 0) {
        free(buf);
        return;
    }

    // Create command
    command *first_cmd = malloc(sizeof(command));
    if (!first_cmd) {
        perror("mysh: Failed to allocate command");
        free(buf);
        return;
    }

    // Initialize command fields
    first_cmd->args = malloc(sizeof(arraylist_t));
    if (!first_cmd->args) {
        perror("mysh: Failed to allocate args");
        free(first_cmd);
        free(buf);
        return;
    }
    init_arrlist(first_cmd->args);
    first_cmd->execpath = NULL;
    first_cmd->inputfile = NULL;
    first_cmd->outputfile = NULL;
    first_cmd->next_command = NULL;

    // Tokenize the command
    char *cursor = buf;
    dyn_string token_value;
    init_dyn_string(&token_value);

    tok_type type;
    command *current_cmd = first_cmd;
    int is_first_token = 1;
    int redir_in = 0;
    int redir_out = 0;
    int last_token_was_pipe = 0;  // Track if last token was a pipe for error handling

    // Doing my tokenizer stuff
    while ((type = next_token(&cursor, &token_value)) != TOK_END) {
        switch (type) {
            case TOK_WORD:
                last_token_was_pipe = 0;  // Reset pipe flag when we see a word for error handling
                if (is_first_token) {
                    // First token is the command/executable path
                    current_cmd->execpath = strdup(token_value.data);
                    is_first_token = 0;
                } else {
                    if (redir_in == 1) {
                        current_cmd->inputfile = strdup(token_value.data);
                        redir_in = 0;
                    }
                    else if (redir_out == 1) {
                        current_cmd->outputfile = strdup(token_value.data);
                        redir_out = 0;
                    }
                    else {
                        // Handle wildcard expansion for each arg
                        if (strchr(token_value.data, '*') != NULL) {
                            glob_t globbuf;
                            int glob_flags = GLOB_NOSORT | GLOB_ERR | GLOB_NOCHECK;
                            
                            int ret = glob(token_value.data, glob_flags, NULL, &globbuf);
                            if (ret == 0) {
                                // If glob found matches, add all of them
                                for (size_t i = 0; i < globbuf.gl_pathc; i++) {
                                    arrlist_push(current_cmd->args, globbuf.gl_pathv[i]);
                                }
                            } else {
                                // If glob fails, add original pattern
                                arrlist_push(current_cmd->args, token_value.data);
                            }
                            globfree(&globbuf);
                        } else {
                            // Regular argument without wildcard
                            arrlist_push(current_cmd->args, token_value.data);
                        }
                    }
                }
                break;

            case TOK_REDIRECT_IN:
                if (redir_in == 1 || redir_out == 1) {
                    fprintf(stderr, "mysh: Multiple redirections not allowed\n");
                    free_command(first_cmd);
                    free_dyn_string(&token_value);
                    free(buf);
                    return;
                }
                redir_in = 1;
                break;

            case TOK_REDIRECT_OUT:
                if (redir_in == 1 || redir_out == 1) {
                    fprintf(stderr, "mysh: Multiple redirections not allowed\n");
                    free_command(first_cmd);
                    free_dyn_string(&token_value);
                    free(buf);
                    return;
                }
                redir_out = 1;
                break;

            case TOK_PIPE: {
                last_token_was_pipe = 1;  // Keep track of the last seen a pipe
                // Create a new command for the next part of the pipe
                command *next_cmd = malloc(sizeof(command));
                if (!next_cmd) {
                    perror("mysh: Failed to allocate command");
                    free_command(first_cmd);
                    free_dyn_string(&token_value);
                    free(buf);
                    return;
                }
                next_cmd->args = malloc(sizeof(arraylist_t));
                if (!next_cmd->args) {
                    perror("mysh: Failed to allocate args");
                    free(next_cmd);
                    free_command(first_cmd);
                    free_dyn_string(&token_value);
                    free(buf);
                    return;
                }
                init_arrlist(next_cmd->args);
                next_cmd->execpath = NULL;
                next_cmd->inputfile = NULL;
                next_cmd->outputfile = NULL;
                next_cmd->next_command = NULL;

                // Link the current command to the next when piping, this is experiment
                current_cmd->next_command = next_cmd;
                current_cmd = next_cmd;
                is_first_token = 1;
                redir_in = 0;
                redir_out = 0;
                break;
            }
            default:
                break;
        }
    }

    // Check for incomplete pipe or redirection before launching command
    if (last_token_was_pipe) {
        fprintf(stderr, "mysh: syntax error: incomplete pipe\n");
        free_command(first_cmd);
        free_dyn_string(&token_value);
        free(buf);
        return;
    }

    if (redir_in || redir_out) {
        fprintf(stderr, "mysh: syntax error: incomplete redirection\n");
        free_command(first_cmd);
        free_dyn_string(&token_value);
        free(buf);
        return;
    }

    // Launch built-in or external commands only if we have a execpath
    if (first_cmd->execpath && strlen(first_cmd->execpath) > 0) {
        launcher(first_cmd, is_interactive);
    }

    // Free resources
    free_dyn_string(&token_value);
    free_command(first_cmd);
    free(buf);
}

int main(int argc, char *argv[]) {
    int input_fd = STDIN_FILENO;
    int is_interactive;
    
    // Check argument count
    if (argc > 2) {
        fprintf(stderr, "mysh: Usage: %s [script_file]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Handle file input if specified
    if (argc == 2) {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd == -1) {
            perror("mysh: Failed to open input file");
            exit(EXIT_FAILURE);
        }
    }

    // Determine if interactive mode
    is_interactive = isatty(input_fd);

    // Print welcome message only in interactive mode
    if (is_interactive) {
        printf("Welcome to myshell!\n");
    }

    while (1) {
        // Print prompt only in interactive mode
        if (is_interactive) {
            printf("mysh> ");
            fflush(stdout);
        }

        // Parse and execute commands
        parse_input(input_fd, is_interactive);
    }

    // Close file if we opened one
    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }

    // Print exiting message only in interactive mode
    if (is_interactive) {
        printf("mysh: Exiting my shell...\n");
    }

    return EXIT_SUCCESS;
}
