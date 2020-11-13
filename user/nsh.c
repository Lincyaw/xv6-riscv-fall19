#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// 输入命令长度最大为1024
char cmd_buf[1024];
char *left, *right;

inline int print(int fd, char *str)
{
    return write(fd, str, strlen(str));
}

// 把'|'换成终结符'\0',返回剩余的字符
char *simple_tok(char *p, char d)
{
    while (*p != '\0' && *p != d)
        p++;
    if (*p == '\0')
        return 0;
    *p = '\0';
    return p + 1;
}

// [in-place]
// trim spaces on both side
char *trim(char *c)
{
    char *e = c;
    while (*e)
        e++;
    while (*c == ' ')
        *(c++) = '\0';
    while (*(--e) == ' ')
        ;
    *(e + 1) = '\0';
    return c;
}

void redirect(int k, int pd[])
{
    close(k);
    dup(pd[k]);
    close(pd[0]);
    close(pd[1]);
}

void handle(char *cmd)
{
    char buf[32][32];
    char *pass[32];
    int argc = 0;

    cmd = trim(cmd);
    // fprintf(2, "cmd: %s\n", cmd);

    for (int i = 0; i < 32; i++)
        pass[i] = buf[i];

    char *c = buf[argc];
    int input_pos = 0, output_pos = 0;
    for (char *p = cmd; *p; p++)
    {
        if (*p == ' ' || *p == '\n')
        {
            *c = '\0';
            argc++;
            c = buf[argc];
        }
        else
        {
            if (*p == '<')
            {
                input_pos = argc + 1;
            }
            if (*p == '>')
            {
                output_pos = argc + 1;
            }
            *c++ = *p;
        }
    }
    *c = '\0';
    argc++;
    pass[argc] = 0;

    // fprintf(2, "inpos: %d, outpos: %d\n", input_pos, output_pos);

    if (input_pos)
    {
        close(0);
        open(pass[input_pos], O_RDONLY);
    }

    if (output_pos)
    {
        close(1);
        open(pass[output_pos], O_WRONLY | O_CREATE);
    }

    char *pass2[32];
    int argc2 = 0;
    for (int pos = 0; pos < argc; pos++)
    {
        if (pos == input_pos - 1)
            pos += 2;
        if (pos == output_pos - 1)
            pos += 2;
        pass2[argc2++] = pass[pos];
    }
    pass2[argc2] = 0;

    if (fork())
    {
        wait(0);
    }
    else
    {
        exec(pass2[0], pass2);
    }
}
/// 递归函数, 递归处理命令
void handle_cmd()
{
    if (left)
    {
        int pd[2];
        pipe(pd);
        // int parent_pid = getpid();
        // fprintf(2, "pid: %d, cmd: %s\n", parent_pid, a);

        if (!fork()) //parent
        {
            // fprintf(2, "%d -> %d source\n", parent_pid, getpid());
            // 把pd写端重定向到标准输出
            if (right)
                redirect(1, pd);
            handle(left);
        }
        else if (!fork()) //子进程中，再fork一下
        {
            // fprintf(2, "%d -> %d sink\n", parent_pid, getpid());
            if (right)
            {
                // 父进程中将写端给到标准输出了,这里的子进程将父进程写的东西读进来
                // 所以将读端给标准输入
                redirect(0, pd);
                left = right;
                right = simple_tok(left, '|');
                // 递归调用
                handle_cmd();
            }
        }

        close(pd[0]);
        close(pd[1]);
        wait(0);
        wait(0);
    }

    exit(0);
}

// a simple shell
int main(int argc, char *argv[])
{
    while (1)
    {
        print(1, "@");
        memset(cmd_buf, 0, 1024);
        // 接受命令
        gets(cmd_buf, 1024);
        // 刚刚初始化为0,如果还是0,则说明没有读进来
        if (cmd_buf[0] == 0) // EOF
            exit(0);
        // 把回车换成字符终结符
        *strchr(cmd_buf, '\n') = '\0';

        if (fork())
        {
            wait(0);
        }
        else
        {
            left = cmd_buf;
            right = simple_tok(left, '|');
            handle_cmd();
        }
    }

    exit(0);
}