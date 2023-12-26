#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_TOKENS 256
#define MAX_COMMAND_LENGTH 256
#define MAX_EXECS 1024
#define MAX_BACKGROUND_PROCESSES 100
#define MAX_ALIASES 100 // Maximum number of aliases


int aliasCount = 0;
int execCount = 0;
int backgroundCount = 0;
pid_t backgroundProcesses[MAX_BACKGROUND_PROCESSES];

struct Exec {
    char *execc;
};
struct Exec execs[MAX_EXECS];

struct Alias {
    char *name;
    char *command;
};
struct Alias aliases[MAX_ALIASES]; // Global array to store aliases


#define ALIAS_FILE "aliases.txt"

// Function to write aliases to a file
void writeAliasesToFile() {
    FILE *file = fopen(ALIAS_FILE, "w");
    if (file == NULL) {
        printf("Error opening file for writing.\n");
        return;
    }

    for (int i = 0; i < aliasCount; ++i) {
        fprintf(file, "alias %s = \"%s\"\n", aliases[i].name, aliases[i].command);
    }

    fclose(file);
}

// Function to update number of processes being executed
void checkBackgroundProcesses() {
    int status;
    pid_t pid;
    for (int i = 0; i < backgroundCount; ++i) {
        pid = waitpid(backgroundProcesses[i], &status, WNOHANG);
        if (pid > 0) {
            kill(backgroundProcesses[i], SIGKILL);
            
            if (i < backgroundCount - 1) {
                backgroundProcesses[i] = backgroundProcesses[backgroundCount - 1];
            }
            backgroundCount--;
        }
    }
}

// Function to print shell prompt in every line
void setup_prompt() {
    struct passwd *pw;
    pw = getpwuid(getuid());
    if (pw == NULL) {
        perror("getpwuid");
        return;
    }

    char hostname[1024];
    gethostname(hostname, sizeof(hostname));

    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    printf("%s@%s %s --- ", pw->pw_name, hostname, cwd);
}

// Function to reverse a string
void reverseString(char *str) {
    int n = strlen(str);
    for (int i = 0; i < n / 2; i++) {
        char temp = str[i];
        str[i] = str[n - i - 1];
        str[n - i - 1] = temp;
    }
}

// Built-in bello
void bello() {
    struct passwd *pw;
    pw = getpwuid(getuid());
    if (pw == NULL) {
        perror("getpwuid");
        return;
    }
    printf("Username: %s\n", pw->pw_name);
    char hostname[1024];
    gethostname(hostname, sizeof(hostname));
    printf("Hostname: %s\n", hostname);
    if (execCount == 0) {
        printf("Last executed command: Nothing\n");
    } else {
        printf("Last executed command: %s\n", execs[execCount].execc);
    }
    char *tty = ttyname(STDIN_FILENO);
    printf("TTY: %s\n", tty);
    char *shell_name = getenv("SHELL");
    printf("Current shell name: %s\n", shell_name);
    char *home = getenv("HOME");
    printf("Home location: %s\n", home);
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    printf("Current time and date: %s\n", buffer);
    
    printf("Current number of processes being executed: %d\n", backgroundCount);
}

// Function for parsing input into tokens
int parser(char commandLine[1024], char *tokens[MAX_TOKENS]) {
    char *cl = (char*)malloc(strlen(commandLine));
    strcpy(cl, commandLine);
    char *token = strtok(cl, " ");
    int count = 0;

    while (token != NULL && count < MAX_TOKENS - 1) {
        tokens[count] = token;
        count++;
        token = strtok(NULL, " ");
    }
    tokens[count] = NULL; // Set the last token to NULL for execvp

    return count;
}

void execute_command(char *tokens[MAX_TOKENS], int tokenCount, int background, char input[]) {
    int WRITE_REVERSE = 0;
    int pipe_fd[2];
    checkBackgroundProcesses();
    for (int i = aliasCount - 1; i >= 0; --i) {    // Check if the input command matches an alias
        if (strcmp(tokens[0], aliases[i].name) == 0) {
            char *alias_tokens[MAX_TOKENS];
            int alias_tokenCount = parser(aliases[i].command, alias_tokens);
            char *tokenExec[MAX_TOKENS];
            int k = 0;
            while (alias_tokens[k] != NULL) {
                tokenExec[k] = alias_tokens[k];
                k++;
            }
            int l = 1;
            while (tokens[l] != NULL) {
                tokenExec[k] = tokens[l];
                k++;
                l++;
            }
            if (strcmp(tokenExec[k - 1], "&") == 0) {
                tokenExec[k - 1] = NULL;
                tokenCount--;
                background = 1;
            }
            tokenExec[k] = NULL;
            execute_command(tokenExec, alias_tokenCount + tokenCount - 1, background, input);
            return;
        }
    }
    for (int i = 0; i < tokenCount; i++) {
        if (strcmp(tokens[i], ">>>") == 0){
            WRITE_REVERSE = 1;
        }
    }
    pipe(pipe_fd);
    pid_t pid = fork();
    int rfile;
    if (pid < 0) {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0) { // Child process
        char *path = getenv("PATH");
        char *tkn = strtok(path, ":");
        int rdr = 0;

        if (tokenCount > 2) {
            if (strcmp(tokens[tokenCount - 2], ">") == 0) {
                rdr = 1;
                rfile = open(tokens[tokenCount - 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                tokens[tokenCount - 2] = '\0';
                tokens[tokenCount - 1] = '\0';
                if (rfile < 0) {
                    // File opening failed
                    fprintf(stderr, "Failed to open file\n");
                    return;
                }
                // Redirect stdout to the file using dup2
                if (dup2(rfile, STDOUT_FILENO) < 0) {
                    // Dup2 failed
                    fprintf(stderr, "Dup2 failed\n");
                    return;
                }
            } else if (strcmp(tokens[tokenCount - 2], ">>") == 0) {
                rdr = 1;
                rfile = open(tokens[tokenCount - 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
                tokens[tokenCount - 2] = '\0';
                tokens[tokenCount - 1] = '\0';
                if (rfile < 0) {
                    // File opening failed
                    fprintf(stderr, "Failed to open file\n");
                    return;
                }
                // Redirect stdout to the file using dup2
                if (dup2(rfile, STDOUT_FILENO) < 0) {
                    // Dup2 failed
                    fprintf(stderr, "Dup2 failed\n");
                    return;
                }
            } else if (strcmp(tokens[tokenCount - 2], ">>>") == 0) {
                close(pipe_fd[0]);
                dup2(pipe_fd[1], STDOUT_FILENO);
                close(pipe_fd[1]);
                tokens[tokenCount - 2] = NULL;
            }
        }
        while (tkn != NULL) {
            char command_path[1024];
            snprintf(command_path, sizeof(command_path), "%s/%s", tkn, tokens[0]);
            if (access(command_path, X_OK) == 0 || strcmp(tokens[0], "bello") == 0) {
                if (strcmp(tokens[0], "bello") == 0) {
                    bello();
                } else {
                    execvp(command_path, tokens);
                    perror("Execution failed");
                }
                if (rdr) {
                    close(rfile);
                }
                exit(EXIT_FAILURE);
            }
            tkn = strtok(NULL, ":");
        }
        printf("Command not found: %s\n", tokens[0]);
        exit(EXIT_SUCCESS);
    }
    else {
            int status;
            execCount++;
            execs[execCount].execc = input;
            if (background) {
                backgroundProcesses[backgroundCount++] = pid;
            } else {
                waitpid(pid, &status, 0);
            }
            if (WRITE_REVERSE) {
                close(pipe_fd[1]);
                char buffer[4096];
                ssize_t count = read(pipe_fd[0], buffer, sizeof(buffer) - 1);
                if (count > 0) {
                    buffer[count] = '\0';
                    reverseString(buffer);
                    int outFile = open(tokens[tokenCount - 1], O_WRONLY | O_CREAT, 0666);
                    if (outFile == -1) {
                        perror("open");
                        exit(EXIT_FAILURE);
                    }
                    write(outFile, buffer, strlen(buffer));
                    close(outFile);
                }
                close(pipe_fd[0]);
            }
        }
}

// Function for parsing alias command lines properly
void parseAlias(const char *input) {
    char *name = NULL;
    char *alias_command = NULL;
    const char *equalsPos = strstr(input, "=");
    if (equalsPos != NULL) {
        const char *nameStart = input + 6; // Skip "alias "
        const char *nameEnd = equalsPos - 1;
        size_t nameLength = nameEnd - nameStart;
        name = (char *)malloc((nameLength + 1) * sizeof(char));

        if (name != NULL) {
            strncpy(name, nameStart, nameLength);
            name[nameLength] = '\0';
        } else {
            printf("Memory allocation failed for alias name.\n");
            return;
        }
        const char *aliasStart = equalsPos + 3; // Skip "= " and quote
        const char *aliasEnd = input + strlen(input) - 1; // Exclude ending quote
        size_t aliasLength = aliasEnd - aliasStart;
        alias_command = (char *)malloc((aliasLength + 1) * sizeof(char));

        if (alias_command != NULL) {
            strncpy(alias_command, aliasStart, aliasLength);
            alias_command[aliasLength] = '\0';
        } else {
            printf("Memory allocation failed for alias command.\n");
            free(name); // Free previously allocated memory for name
            return;
        }
        // Add the alias to the list
        if (aliasCount < MAX_ALIASES) {
            aliases[aliasCount].name = name;
            aliases[aliasCount].command = alias_command;
            aliasCount++;
            writeAliasesToFile();

        } else {
            printf("Maximum number of aliases reached.\n");
            free(name);
            free(alias_command);
        }
    }
}

// Function for copying a string into another
char* copyString(char s[]) {
    char *s2, *p1, *p2;
    s2 = (char*)malloc(1024);
    p1 = s;
    p2 = s2;
 
    // Executing till the null
    // character is found
    while (*p1 != '\0') {
        // Copy the content of s1 to s2
        *p2 = *p1;
        p1++;
        p2++;
    }
    *p2 = '\0';
 
    return s2;
}


int main() {
    // Code for main remains mostly the same
    char *tokens[MAX_TOKENS];
    char input[1024];
    execs[0].execc = "\n";

    if (access(ALIAS_FILE, F_OK) == 0) {
        char * line = NULL;
        size_t len = 0;
        ssize_t read;
        FILE *file = fopen(ALIAS_FILE, "r");
        while ((read = getline(&line, &len, file)) != -1) {
            line[strcspn(line, "\n")] = '\0';
            parseAlias(line);
    }

    fclose(file);
    }

    while (1) {
        int background = 0;
        setup_prompt();
        if (fgets(input, 1024, stdin) == NULL){
            printf("\n");
            exit(0);
        }
        input[strcspn(input, "\n")] = '\0';
        char *inp;
        inp = copyString(input);
        strcpy(inp, input);

        if (input[0] == '\0') {
            continue;
        }
        if (strcmp(input, "exit") == 0) {
            break;
        }
        if (strncmp(input, "alias", 5) == 0) {
            execCount++;
            execs[execCount].execc = inp;
            parseAlias(input);
        } else {
            if (input[strlen(input) - 1] == '&') {
                input[strlen(input) - 1] = '\0';
                background = 1;
            }
            int tokenCount = parser(input, tokens);
            execute_command(tokens, tokenCount, background, inp);
        }
    }

    // Free allocated memory for aliases before exiting
    for (int i = 0; i < aliasCount; ++i) {
        free(aliases[i].name);
        free(aliases[i].command);
    }

    return 0;
}
