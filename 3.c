#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>

void *ft(void *arg) {
    for(int i=0;i<2;i++) {
        printf("%d %d %s\n", getpid(), i, (const char *)arg);
        usleep(100);
    }
    return NULL;
}

int main() {
    pthread_t t1, t2;
    pthread_create(&t1, NULL, ft, (void*)"t1");
    pthread_create(&t2, NULL, ft, (void*)"t2");

    printf("a\n");
    pid_t rv = fork();
    printf("b %d\n", rv);
    usleep(1000);
    if(rv != 0) wait(NULL);
    return 0;
}
