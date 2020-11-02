#include "kernel/types.h"
#include "user/user.h"
/**
 *    https://blog.csdn.net/u013319359/article/details/81091176 
**/

/**
 * 产生数字，最顶层进程
 **/
void createNumber()
{
    for (int i = 2; i < 36; i++)
    {
        write(1, &i, sizeof(i));
    }
}
/**
 * 重定向函数， 将标准输入或标准输出 对应为 管道的标准输入或者标准输出
 **/
void redirect(int i, int pipeline[])
{
    close(i);           // 关闭i端口
    dup(pipeline[i]);   // 将pipeline[i]端口给i
    close(pipeline[0]); // 关闭管道
    close(pipeline[1]);
}
/**
 * 判断是否为质数的函数
 **/
void judge()
{
    int pipeline[2];
    int i;
    pipe(pipeline);
    if (read(0, &i, sizeof(i)))
    {
        printf("prime %d\n", i); // 传进来的第一个数一定是质数
        if (fork() == 0)
        {
            // chilid
            redirect(0, pipeline);
            judge();
        }
        else
        {
            // parent
            redirect(1, pipeline);
            int n;
            while (read(0, &n, sizeof(n))) // 父进程，则判断出所有不是i的倍数的，传入管道
            {                              // 方便子进程继续判断。
                if (n % i != 0)
                {
                    write(1, &n, sizeof(n));
                }
            }
        }
    }
}
int main(int argc, char *argv[])
{
    int pipeline[2];
    pipe(pipeline);
    printf("begins:  \n");
    if (fork() == 0)
    {
        // 如果是子进程， 则递归判断
        redirect(0, pipeline);
        judge();
    }
    else
    { //如果是父进程，则产生数字，向管道写入数据。
        redirect(1, pipeline);
        createNumber();
    }
    exit();
}
