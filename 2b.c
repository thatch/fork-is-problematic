#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    char buf[128] = "\0";
    char *ptr = (char*) buf;

    ptr += sprintf(ptr, "parent %d\n", getpid());

    /* this is roughly the library function flush() */
    write(1, buf, strlen(buf));
    ptr = (char*) buf;
    *ptr = 0;

    pid_t rv = fork();

    ptr += sprintf(ptr, "child %d\n", getpid());
    *ptr = 0;

    write(1, buf, strlen(buf));

    if(rv != 0) wait(NULL);

    return 0;
}
