#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    char buf[32][32];
    char *pass[32];
    char buf2[512];

    for (int i = 0; i < 32; i++) // 使pass和buf指向同一片地址, pass[0]是命令,后面的是参数
        pass[i] = buf[i];

    for (int i = 1; i < argc; i++) // 将传进来的参数存进该地址开始的内存
        strcpy(buf[i - 1], argv[i]);

    int n;
    // 读入接下来输入的参数, 指的是单独使用xargs时
    // xargs将从标准输入读取行,并为每行运行一个命令,
    // 且将该行作为命令的参数运行
    while ((n = read(0, buf2, sizeof(buf2))) > 0)
    {
        int pos = argc - 1;
        char *c = buf[pos];           // 令c为输入的参数中的最后一个命令的下一个命令的第一个字符
                                      // 注意: argc中的n个参数中,第一个参数是程序的名称以及所在的路径,
                                      // 第后面的参数是输入的参数
                                      // 在上面14行将第1个参数移到了第0个,所以这里从argc-1个开始就是没有定义的参数了
        for (char *p = buf2; *p; p++) // 继续接受参数
        {
            if (*p == ' ' || *p == '\n') // 遇到换行或者空格就结束读取参数, 继续添加下一个参数
            {
                *c = '\0';
                pos++;
                c = buf[pos];
            }
            else
            {
                *c = *p;
                c++;
            }
        }
        *c = '\0';
        pos++;
        pass[pos] = 0;

        if (fork())
        {
            wait();
        }
        else
            exec(pass[0], pass); // 这时所有参数已经都放到了1行里
    }

    if (n < 0)
    {
        printf("xargs: read error\n");
        exit();
    }

    exit();
}