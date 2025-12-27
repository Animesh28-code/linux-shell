#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAXCOM 1000
#define MAXLIST 100

static void printPrompt(void) {
    char cwd[1024];
    const char *user = getenv("USER");
    if (!user) user = "user";

    if (!getcwd(cwd, sizeof(cwd)))
        strcpy(cwd, "?");

    printf("\n\033[1;32m%s@myshell\033[0m:\033[1;34m%s\033[0m$ ",
           user, cwd);
    fflush(stdout);
}

static int takeInput(char *str) {
    char *buf = readline("\n>>> ");
    if (!buf) return 1;
    if (strlen(buf) != 0) {
        add_history(buf);
        strcpy(str, buf);
        free(buf);
        return 0;
    }
    free(buf);
    return 1;
}

static void parseSpace(char *str, char **parsed) {
    int i = 0;
    while (i < MAXLIST - 1) {
        char *tok = strsep(&str, " ");
        if (!tok) break;
        if (*tok == '\0') continue;
        parsed[i++] = tok;
    }
    parsed[i] = NULL;
}

static int parsePipe(char *str, char **strpiped) {
    strpiped[0] = str;
    strpiped[1] = NULL;
    for (char *p = str; *p; ++p) {
        if (*p == '|') {
            *p = '\0';
            strpiped[1] = p + 1;
            return 1;
        }
    }
    return 0;
}

static void handleRedirection(char **args, int *in_fd, int *out_fd) {
    *in_fd = -1;
    *out_fd = -1;

    for (int i = 0; args[i] != NULL; ) {
        if (strcmp(args[i], "<") == 0) {
            if (!args[i + 1]) { fprintf(stderr, "Syntax error: expected file after <\n"); return; }
            int fd = open(args[i + 1], O_RDONLY);
            if (fd < 0) { perror("open <"); return; }
            *in_fd = fd;
            for (int j = i; args[j] != NULL; j++) args[j] = args[j + 2];
            continue;
        }

        if (strcmp(args[i], ">") == 0) {
            if (!args[i + 1]) { fprintf(stderr, "Syntax error: expected file after >\n"); return; }
            int fd = open(args[i + 1], O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd < 0) { perror("open >"); return; }
            *out_fd = fd;
            for (int j = i; args[j] != NULL; j++) args[j] = args[j + 2];
            continue;
        }

        if (strcmp(args[i], ">>") == 0) {
            if (!args[i + 1]) { fprintf(stderr, "Syntax error: expected file after >>\n"); return; }
            int fd = open(args[i + 1], O_CREAT | O_WRONLY | O_APPEND, 0644);
            if (fd < 0) { perror("open >>"); return; }
            *out_fd = fd;
            for (int j = i; args[j] != NULL; j++) args[j] = args[j + 2];
            continue;
        }

        i++;
    }
}

static void execWithRedir(char **args) {
    int in_fd, out_fd;
    handleRedirection(args, &in_fd, &out_fd);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return; }

    if (pid == 0) {
        if (in_fd != -1) { dup2(in_fd, STDIN_FILENO); close(in_fd); }
        if (out_fd != -1) { dup2(out_fd, STDOUT_FILENO); close(out_fd); }
        execvp(args[0], args);
        perror("execvp");
        _exit(127);
    } else {
        if (in_fd != -1) close(in_fd);
        if (out_fd != -1) close(out_fd);
        wait(NULL);
    }
}

static void execPipedWithRedir(char **left, char **right) {
    int in1, out1, in2, out2;
    handleRedirection(left, &in1, &out1);
    handleRedirection(right, &in2, &out2);

    int pipefd[2];
    if (pipe(pipefd) < 0) { perror("pipe"); return; }

    pid_t p1 = fork();
    if (p1 < 0) { perror("fork"); return; }

    if (p1 == 0) {
        if (in1 != -1) { dup2(in1, STDIN_FILENO); close(in1); }
        if (out1 != -1) { dup2(out1, STDOUT_FILENO); close(out1); }
        else dup2(pipefd[1], STDOUT_FILENO);

        close(pipefd[0]); close(pipefd[1]);
        execvp(left[0], left);
        perror("execvp left");
        _exit(127);
    }

    pid_t p2 = fork();
    if (p2 < 0) { perror("fork"); return; }

    if (p2 == 0) {
        if (out2 != -1) { dup2(out2, STDOUT_FILENO); close(out2); }
        if (in2 != -1) { dup2(in2, STDIN_FILENO); close(in2); }
        else dup2(pipefd[0], STDIN_FILENO);

        close(pipefd[1]); close(pipefd[0]);
        execvp(right[0], right);
        perror("execvp right");
        _exit(127);
    }

    close(pipefd[0]); close(pipefd[1]);
    if (in1 != -1) close(in1);
    if (out1 != -1) close(out1);
    if (in2 != -1) close(in2);
    if (out2 != -1) close(out2);
    waitpid(p1, NULL, 0);
    waitpid(p2, NULL, 0);
}


static void openHelp(void) {
    puts("\n*** MY SHELL HELP ***\n"
         "Builtins:\n"
         "  cd <dir>\n"
         "  exit\n"
         "  help\n"
         "Supports:\n"
         "  normal commands\n"
         "  single pipe: cmd1 | cmd2\n"
         "  redirection: >  <  >>\n");
}

static int ownCmdHandler(char **parsed) {
    if (!parsed[0]) return 1;

    if (strcmp(parsed[0], "exit") == 0) {
        puts("Goodbye!");
        exit(0);
    }
    if (strcmp(parsed[0], "cd") == 0) {
        if (!parsed[1]) fprintf(stderr, "cd: missing argument\n");
        else if (chdir(parsed[1]) != 0) perror("cd");
        return 1;
    }
    if (strcmp(parsed[0], "help") == 0) {
        openHelp();
        return 1;
    }
    return 0;
}

static int processString(char *str, char **parsed, char **parsedpipe) {
    char *strpiped[2];
    int piped = parsePipe(str, strpiped);

    if (piped) {
        parseSpace(strpiped[0], parsed);
        parseSpace(strpiped[1], parsedpipe);
    } else {
        parseSpace(str, parsed);
    }

    if (ownCmdHandler(parsed)) return 0;
    return 1 + piped;
}

int main(void) {
    char inputString[MAXCOM];
    char *parsedArgs[MAXLIST];
    char *parsedArgsPiped[MAXLIST];
    int execFlag = 0;
    while (1) {
    printPrompt();
    if (takeInput(inputString))
        continue;

    execFlag = processString(inputString, parsedArgs, parsedArgsPiped);

    if (execFlag == 1)
        execWithRedir(parsedArgs);

    if (execFlag == 2)
        execPipedWithRedir(parsedArgs, parsedArgsPiped);
}

    return 0;
}
