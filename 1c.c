#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

int main() {
    printf("hi!\n");
    kill(getpid(), SIGTERM);

    return 0; /* noreturn */
}
