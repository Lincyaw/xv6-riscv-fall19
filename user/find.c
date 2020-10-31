#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 来自grep.c的正则表达式匹配

int matchhere(char *, char *);
int matchstar(int, char *, char *);
int match(char *re, char *text)
{
    if (re[0] == '^') // 匹配输入字符串的开始位置,表示匹配以'^'之后的字符串开头的文字
        return matchhere(re + 1, text);
    do
    { // must look at empty string
        if (matchhere(re, text))
            return 1;
    } while (*text++ != '\0');
    return 0;
}

// matchhere: search for re at beginning of text
// 匹配上返回1, 没匹配上返回0
int matchhere(char *re, char *text)
{
    if (re[0] == '\0') // 传进来的第一个字符就是结尾， 就说明匹配结束了
        return 1;
    if (re[1] == '*') // 注意当前的指针re指向的是0位置，这里判断如果1位置是'*',则传入的字符串应该是re+2之后的
        return matchstar(re[0], re + 2, text);
    if (re[0] == '$' && re[1] == '\0') // 匹配结尾的字符，'$'表示结尾
        return *text == '\0';
    if (*text != '\0' && (re[0] == '.' || re[0] == *text)) // '.'匹配除换行符之外的任何字符
        return matchhere(re + 1, text + 1);
    return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char *re, char *text)
{
    do
    { // a * matches zero or more instances
        if (matchhere(re, text))
            return 1;
    } while (*text != '\0' && (*text++ == c || c == '.')); //没到末尾并且开头的那个字符是对的  匹配'c*re'
    return 0;
}
char *fmtname(char *path)
{
    static char buf[DIRSIZ + 1];
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
    return buf;
}

void find(char *path, char *re)
{

    int fd;
    struct stat st;
    struct dirent de;
    char buf[512], *p;

    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) // 成功返回0,失败返回-1
    {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    // printf("type: %d\n", st.type);
    switch (st.type)
    {
    case T_FILE:
        if (match(re, fmtname(path)))
            printf("%s\n", path);
        break;
    case T_DIR:
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
        {
            printf("ls: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if (de.inum == 0)
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if (stat(buf, &st) < 0)
            {
                printf("ls: cannot stat %s\n", buf);
                continue;
            }
            // printf("%s\n", fmtname(buf));
            if (strlen(de.name) == 1 && de.name[0] == '.')
                continue;
            if (strlen(de.name) == 2 && de.name[0] == '.' && de.name[1] == '.')
                continue;

            find(buf, re);
        }
        break;
    default:
        break;
    }
    close(fd);
}

int main(int argc, char *argv[])
{
    // printf("begins:  \n");
    // if (argc <= 2)
    //     fprintf(2, "Not enough params!!!\n");
    find(argv[1], argv[2]);
    exit();
}
