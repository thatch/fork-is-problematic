#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    pid_t rv = fork();
   
    char buf[128];
    char *ptr = (char*) buf;

    for(int i=0;i<3;i++) { 
        ptr += read(0, ptr, 1);
        usleep(100);
    }
    *ptr = 0;

    printf("%s\n", buf);
    if(rv != 0) wait(NULL);

    return 0;
}
