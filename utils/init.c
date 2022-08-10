#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    setenv("TERM", "linux", 1);
    setenv("USER", "root", 1);
    setenv("HOME", "/root", 1);
    setenv("PATH", "/bin:/usr/bin:/usr/local/bin", 1);

    for (;;) {
        printf("\x1b[2J\x1b[H");
        printf("Welcome to Lyre!\n");
        printf("You can find the source code at https://github.com/lyre-os/lyre\n\n");

        int pid = fork();
        if (pid == 0) {
            char *argv[] = {"/usr/bin/bash", "-l", NULL};

            chdir(getenv("HOME"));
            execvp("/usr/bin/bash", argv);
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
    }

    return 0;
}
