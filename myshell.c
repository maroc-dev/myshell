#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>

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

// Custom strdup implementation
char *strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

// Initialize dynamic string
void init_dyn_string(dyn_string *str) {
    str->data = malloc(10);  // Initial capacity
    if (!str->data) {
        perror("Failed to allocate dynamic string");
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
            perror("Failed to resize dynamic string");
            exit(EXIT_FAILURE);
        }
        str->data = new_data;
    }
}

// Append to dynamic string
void append_dyn_string(dyn_string *str, char c) {
    ensure_dyn_string_capacity(str, 2);  // 1 for char, 1 for null terminator
    str->data[str->size++] = c;
    str->data[str->size] = '\0';
}

// Free dynamic string
void free_dyn_string(dyn_string *str) {
    free(str->data);
    str->data = NULL;
    str->size = 0;
    str->capacity = 0;
}

// Allocate and initialize arraylist
void init_arrlist(arraylist_t *al) {
    al->size = 0;
    al->capacity = 10;  // Initial capacity
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
            // Free existing memory and exit
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

// Free command structure
void free_command(command *c) {
    if (c == NULL) return;

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
    }
    free(c);
}

int search_exec(command *c) {
    const char *allowed_dirs[] = {"/usr/local/bin", "/usr/bin", "/bin"};
    size_t num_dirs = sizeof(allowed_dirs) / sizeof(allowed_dirs[0]);

    char *cmd = c->execpath;

    // Check if command contains a slash (explicit path)
    if (strchr(cmd, '/') != NULL) {
        // Treat as an explicit path
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
            perror("realloc failed");
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

void execute_external_command(command *c) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        return;
    } else if (pid == 0) {  // Child process
        // Handle input redirection
        if (c->inputfile) {
            int input_fd = open(c->inputfile, O_RDONLY);
            if (input_fd < 0) {
                perror("Input file open failed");
                return;
            }
            if (dup2(input_fd, STDIN_FILENO) < 0) {
                perror("dup2 for input redirection failed");
                close(input_fd);
                return;
            }
            close(input_fd); // Close the original file descriptor
        }

        // Handle output redirection
        if (c->outputfile) {
            int output_fd = open(c->outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (output_fd < 0) {
                perror("Output file open failed");
                exit(EXIT_FAILURE);
            }
            if (dup2(output_fd, STDOUT_FILENO) < 0) {
                perror("dup2 for output redirection failed");
                close(output_fd);
                exit(EXIT_FAILURE);
            }
            close(output_fd); // Close the original file descriptor
        }

        // Prepare arguments for execv
        char **argv = malloc((c->args->size + 2) * sizeof(char *));
        if (!argv) {
            perror("Memory allocation failed");
            exit(EXIT_FAILURE);
        }
        int err = search_exec(c);

        if (err != 0){
            perror("Command not found");
            free(argv);
            return;
        }

        argv[0] = c->execpath;
        for (size_t i = 0; i < c->args->size; i++) {
            argv[i + 1] = c->args->val[i];
        }
        argv[c->args->size + 1] = NULL;

        // Execute the command
        execv(c->execpath, argv);

        // If execv fails
        perror("Command execution failed");
        free(argv);
        exit(EXIT_FAILURE);
    } else {  // Parent process
        int status;
        waitpid(pid, &status, 0);
    }
}

// Command implementations
void myexit(command *c) {
    for (int i = 0; i < c->args->size; i++) {
        printf("%s ", c->args->val[i]);
    }

    printf("\nExiting shell...\n");
    free_command(c);
    exit(EXIT_SUCCESS);
}



void mywhich(command *c) {
    // Ensure there is exactly one argument in args
    if (c->args->size != 1) {
        fprintf(stderr, "Improper usage of which: which <command>\n");
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
            perror("realloc failed");
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

void mypwd(command *c) {
    char *cwd = getcwd(NULL, 0);
    if (cwd != NULL) {
        printf("%s\n", cwd);
        free(cwd);
    } else {
        fprintf(stderr, "pwd: getcwd error");
        return;
    }
}

void mycd(command *c) {
    if (c->args->size != 1) {
        fprintf(stderr, "Improper usage of cd: cd <directory>\n");
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

// Launcher for built-in commands
void launcher(command *c) {
    // exit command
    if (strcmp(c->execpath, "exit") == 0) {
        myexit(c);
    }
    // which command 
    else if (strcmp(c->execpath, "which") == 0) {
        mywhich(c);
        return;
    }
    // pwd command 
    else if (strcmp(c->execpath, "pwd") == 0) {
        mypwd(c);
        return;
    }
    // cd command 
    else if (strcmp(c->execpath, "cd") == 0) {
        mycd(c);
        return;
    }
    // For non-built-in commands, try to execute externally
    else {
        execute_external_command(c);
    }
}

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
    
    // Word/argument parsing
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
    
    // Unexpected case
    *cursor = start;
    return TOK_END;
}

void parse_input(int fd) {
    int bufsize = 4;  // Start with small initial size
    int total_bytes_read = 0;
    char *buf = malloc(bufsize);
    if (buf == NULL) {
        perror("Buffer allocation failed");
        return;
    }

    // Dynamic buffer reading
    while (1) {
        // Ensure space for reading and null termination
        if (total_bytes_read >= bufsize - 1) {
            bufsize *= 2;
            char *new_buf = realloc(buf, bufsize);
            if (new_buf == NULL) {
                perror("Buffer reallocation failed");
                free(buf);
                return;
            }
            buf = new_buf;
        }

        // Read into the remaining buffer space
        int bytes_read = read(fd, buf + total_bytes_read, bufsize - total_bytes_read - 1);
        
        if (bytes_read <= 0) {
            // No more input or error
            if (total_bytes_read == 0) {
                free(buf);
                return;
            }
            break;
        }

        total_bytes_read += bytes_read;
        buf[total_bytes_read] = '\0';

        // Check if we've read a full line
        if (memchr(buf, '\n', total_bytes_read) != NULL) {
            break;
        }
    }

    // Trim trailing newline
    if (total_bytes_read > 0 && buf[total_bytes_read - 1] == '\n') {
        buf[total_bytes_read - 1] = '\0';
        total_bytes_read--;
    }

    // Skip completely empty input
    if (total_bytes_read == 0) {
        free(buf);
        return;
    }

    // Create command structure
    command *c = malloc(sizeof(command));
    if (!c) {
        perror("Failed to allocate command");
        free(buf);
        return;
    }

    // Initialize command fields
    c->args = malloc(sizeof(arraylist_t));
    init_arrlist(c->args);
    c->execpath = NULL;
    c->inputfile = NULL;
    c->outputfile = NULL;

    // Tokenize the command
    char *cursor = buf;
    dyn_string token_value;
    init_dyn_string(&token_value);

    tok_type type;
    int is_first_token = 1;
    int redir_in = 0;
    int redir_out = 0;

    while ((type = next_token(&cursor, &token_value)) != TOK_END) {
        switch (type) {
            case TOK_WORD:
                if (is_first_token) {
                    // First token is the command/executable path
                    c->execpath = strdup(token_value.data);
                    is_first_token = 0;
                } else {

                    if (redir_in == 1){
                        c->inputfile = strdup(token_value.data);
                        redir_in = 0;
                        break;
                    }
                    else if (redir_out == 1){
                        c->outputfile = strdup(token_value.data);
                        redir_in = 0;
                        break;
                    }

                    // Subsequent tokens are arguments
                    arrlist_push(c->args, token_value.data);
                }
                break;
            case TOK_REDIRECT_IN:
                if (redir_in == 1 || redir_out == 1){
                    fprintf(stdout, "Invalid syntax for redirection in: expected file");
                    free_dyn_string(&token_value);
                    free_command(c);
                    free(buf);
                    return;
                }
                redir_in = 1;
                break;
            case TOK_REDIRECT_OUT:
            if (redir_in == 1 || redir_out == 1){
                    fprintf(stdout, "Invalid syntax for redirection out: expected file");
                    free_dyn_string(&token_value);
                    free_command(c);
                    free(buf);
                    return;
                }
                redir_out = 1;
                break;
            case TOK_PIPE:
                break;
            case TOK_NEWLINE:
                break;
            default:
                break;
        }
    }

    // Launch built-in or external commands only if we have a valid execpath
    if (c->execpath && strlen(c->execpath) > 0) {
        launcher(c);
    }

    // Free resources
    free_dyn_string(&token_value);
    free_command(c);
    free(buf);
}

int main() {
    printf("Welcome to myshell!\n");
    
    while (1) {
        printf("mysh> ");
        fflush(stdout);
        parse_input(_stdin);
    }
    
    return EXIT_SUCCESS;
}