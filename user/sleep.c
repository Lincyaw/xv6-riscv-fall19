#include "kernel/types.h"
#include "user/user.h"


int main(int argc, char *argv[])
{
    int i;
    if (argc < 2)
    {
        printf("参数不足, 请输入sleep的时间\n");
        exit();
    }else if(argc>2){
        printf("彩蛋来啦！！\n");
        for (i = 1; i < argc; i++)
        printf("参数%d = %s\n",i,argv[i]);
    }
    int n = atoi(argv[1]);
    if (n > 0){
		sleep(n);
	} else {
		printf("Invalid interval %s\n", argv[1]);
	}
    exit();
}
