//https://blog.csdn.net/zhangye3017/article/details/80189861

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
int main()
{
    // char *argv[2];
    // argv[0] = "wc";
    // argv[1] = 0;
    int p[2];
    pipe(p);
    printf("pipe begin: p[0]= %d , p[1]=%d \n", p[0], p[1]);
    pipe(p);
    printf("pipe begin: p[0]= %d , p[1]=%d \n", p[0], p[1]);
    pipe(p);
    printf("pipe begin: p[0]= %d , p[1]=%d \n", p[0], p[1]);
    pipe(p);
    printf("pipe begin: p[0]= %d , p[1]=%d \n", p[0], p[1]);

    printf("dup begin : %d\n", dup(p[0]));
    close(0);
    printf("%d\n", dup(p[0]));

    int t[2];
    printf("tpipe begin == %d\n", pipe(t));
    printf("pipe begin: t[0]= %d , t[1]=%d \n", t[0], t[1]);

    close(p[0]);
    close(p[1]);
    printf("close begin: p[0]= %d , p[1]=%d \n", p[0], p[1]);

    close(p[0]);
    close(p[1]);
    printf("close begin: p[0]= %d , p[1]=%d \n", p[0], p[1]);

    close(p[0]);
    close(p[1]);
    printf("close begin: p[0]= %d , p[1]=%d \n", p[0], p[1]);

    write(p[1], "fuck", sizeof("fuck"));
    char buf[64];
    read(p[0], buf, sizeof(buf));
    printf("%s", buf);
    // if (fork() == 0)
    // {
    //     close(0);    // 如果是子进程，就把0关了，下面要用0端口读入
    //     dup(p[0]);   // 将p管道的标准输出给0端口
    //     close(p[0]); //把p的标准输入和输出关了。
    //     close(p[1]);
    //     int t;
    //     while (read(0, &t, sizeof(t)))
    //     {
    //         printf("%d\n", t);
    //     }
    //     //exec("wc", argv);
    // }
    // else
    // {
    //     for (int i = 0; i < 36; i++)
    //     {
    //         write(p[1], &i, sizeof(i));
    //     }
    //     // int t = 2;
    //     // write(p[1], &t, sizeof(t));
    //     close(p[0]);
    //     close(p[1]);
    // }
    exit();
}