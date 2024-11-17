#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE 255
#define INITIAL_CAPACITY 10
#define _stdin 0

typedef struct {
    char **val;
    size_t size;
    size_t capacity;
} arraylist_t;

typedef struct command {
    arraylist_t *args;
    char *execpath;
    char *inputfile;
    char *outputfile;
} command;

char *mystrdup(const char *s) {
    char *dup = malloc(strlen(s) + 1);
    if (dup) {
        strcpy(dup, s);
    }
    return dup;
}

void init_arrlist(arraylist_t *al) {
    al->size = 0;
    al->capacity = INITIAL_CAPACITY;
    al->val = malloc(INITIAL_CAPACITY * sizeof(char *));
    if (al->val == NULL) {
        exit(EXIT_FAILURE);
    }
}

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
    free(al);
}

void arrlist_push(arraylist_t *al, char *item) {
    if (al->size == al->capacity) {
        al->capacity *= 2;
        char **new_val = realloc(al->val, al->capacity * sizeof(char *));
        if (new_val == NULL) {
            free_arrlist(al);
            exit(EXIT_FAILURE);
        }
        al->val = new_val;
    }
    al->val[al->size] = mystrdup(item);
    if (al->val[al->size] == NULL) {
        free_arrlist(al);
        exit(EXIT_FAILURE);
    }
    al->size++;
}

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
        c->args = NULL;
    }
    free(c);
}

void launcher(command *c){

    // prebuilt myshell commands

    // exit command
    if (strcmp(c->execpath, "exit") == 0) {

        for (int i = 0; i < c->args->size; i++){
            printf("%s ", c->args->val[i]);
        }

        printf("\nExiting shell...\n");
        free_command(c);
        exit(EXIT_SUCCESS);
    }
    else if (strcmp(c->execpath, "which") == 0){
        if (c->args->size != 1){
            fprintf(stderr, "Improper usage of which: which <command>\n");
            return;
        }

        char *path = getenv("PATH");

        if (path != NULL) {
            fprintf(stderr, "PATH: %s\n", path);
        } else {
            perror("PATH variable not found.\n");
            return;
        }

        printf("%s\n", path);

    }
}

void tokenize(char *buf, int should_exit) {
    char *token = strtok(buf, " ");
    command *c = malloc(sizeof(command));
    if (!c) {
        perror("Failed to allocate memory for command");
        exit(EXIT_FAILURE);
    }
    
    // Initialize all fields
    c->args = NULL;
    c->execpath = NULL;
    c->inputfile = NULL;
    c->outputfile = NULL;
    
    c->args = malloc(sizeof(arraylist_t));
    if (!c->args) {
        perror("Failed to allocate memory for args");
        free_command(c);
        exit(EXIT_FAILURE);
    }
    
    init_arrlist(c->args);
    
    for (int i = 0; token != NULL; i++) {
        printf("Token: %s\n", token);
        if (i == 0) {
            c->execpath = mystrdup(token);
            token = strtok(NULL, " ");
            continue;
        }
        arrlist_push(c->args, token);
        token = strtok(NULL, " ");
    }
    
    launcher(c);

    free_command(c);
}



void get_args(int fd) {
    int bytes, wordstart, pos;
    int bufsize = BUFSIZE;
    char *buf = malloc(bufsize);
    if (buf == NULL) {
        perror("Buffer allocation failed");
        return;
    }
    
    pos = 0;
    wordstart = 0;
    
    while ((bytes = read(fd, buf + pos, bufsize - pos)) > 0) {
        int bufend = pos + bytes;
        for (; pos < bufend; pos++) {
            if (buf[pos] == '\n') {
                buf[pos] = '\0';
                if (wordstart < pos) {
                    tokenize(buf + wordstart, 0);
                    free(buf);
                    return;
                }
                wordstart = pos + 1;
            }
        }
        
        if (wordstart == pos) {
            pos = 0;
        } else if (wordstart > 0) {
            int seglength = pos - wordstart;
            memmove(buf, buf + wordstart, seglength);
            pos = seglength;
        } else if (pos >= bufsize - 2) {
            bufsize *= 2;
            char *newbuf = realloc(buf, bufsize);
            if (newbuf == NULL) {
                free(buf);
                perror("Buffer reallocation failed");
                return;
            }
            buf = newbuf;
        }
        wordstart = 0;
    }
    
    if (wordstart < pos) {
        buf[pos] = '\0';
        if (pos > wordstart) {
            tokenize(buf + wordstart, 0);
        }
    }
    free(buf);
}

int main() {
    printf("Welcome to myshell!\n");
    
    while (1) {
        printf("mysh> ");
        fflush(stdout);
        get_args(_stdin);
    }
    
    return EXIT_SUCCESS;
}