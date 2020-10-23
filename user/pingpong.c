#include "kernel/types.h"
#include "user/user.h"
int main(int argc, char *argv[])
{
    int  pid;
    int parent[2], child[2];
    pipe(parent);
    pipe(child);
    char buf[64];

    if ((pid=fork())==0)
    {
        // Child
        read(parent[0], buf, 4);
        printf("%d: received %s\n", getpid(), buf);
        write(child[1], "pong", strlen("pong"));
       
    }
    else
    {
        // Parent
        write(parent[1], "ping", strlen("ping"));
        read(child[0], buf, 4);
        printf("%d: received %s. childpid = %d\n", getpid(), buf, pid);
    }

    exit();
}