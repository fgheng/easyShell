#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>

#define MAXLINE 128     // 命令行的最大输入
#define MAXCMD  64      // 命令的最大参数量
#define PATHLEN 1024    // 路径的最大长度
#define BACK    1024    // 后台允许执行的进程数量

static struct {
    int num;
    pid_t pids[BACK];
} back_run;                   // 后台运行的进程
static pid_t current_run = 0; // 当前正在执行的进程

// SIGINT信号控制
static void sig_int(int);

void parse_line(char *, char **);
void eval(char **);
int inline_command(char **, char *, int);
int outer_command(char **, char *, int);

int main(int argc, char *argv[])
{
    char buf[MAXLINE];
    char *args[MAXCMD];
    char path[PATHLEN];

    // 初始化后台运行数量
    back_run.num = 0;

    // 用户通过给定环境变量promot来更改命令提示符
    // 如果没有指定，那么默认是$
    char *promot = getenv("promot");
    if (promot == NULL) {
        promot = "$";
    }

    // 注册ctrl c为一个信号
    if (signal(SIGINT, sig_int) == SIG_ERR)
        printf("error");

    getcwd(path, PATHLEN);
    printf("%s %s ", path, promot);

    while(fgets(buf, MAXLINE, stdin) != NULL) {
        // 读取命令行输入，并替换最后的\n为0，表示字符串结束
        size_t buf_len = strlen(buf);

        if (buf_len <= 1) {
            printf("%s %s ", path, promot);
            continue;
        }

        if (buf[buf_len - 1] == '\n')
            buf[strlen(buf) - 1] = 0;

        // 解析命令
        parse_line(buf, args);

        // 运行命令
        eval(args);

        getcwd(path, PATHLEN);
        printf("%s %s ", path, promot);
    }

    return 0;
}


// 命令行字符串解析，以空格为分割单位
void parse_line(char *buf, char **argv) {
    int idx_argc = 0;

    // 遍历命令行字符串
    while (*buf) {
        int idx = 0;
        // 跳过每个参数开头多余的空格
        while (*buf == ' ') {
            buf++;
        }

        // 以空格为单位进行分割
        while (buf[idx] != ' ' && buf[idx] != '\0') idx++;

        // 遍历结束
        if (buf[idx] == '\0') {
            argv[idx_argc++] = buf;
            break;
        }

        buf[idx] = '\0';
        // 将参数赋值给argv，同时参数坐标后移
        argv[idx_argc++] = buf;
        // buf移动到下一个参数位置
        buf += idx + 1;
    }

    // 最后的参数要用NULL
    argv[idx_argc] = NULL;
}

// 执行命令
void eval(char **cmd) {
    int idx = 0;
    int redirect = -1; // 重定向0 stdin 1 stdout 2 stderr
    char *out = NULL;  // 重定向文件

    // 判断是否重定向，如果是的话进行重定向
    while(cmd[idx]) {
        // 输出重定向
        if (**(cmd+idx) == '>') {
            out = cmd[idx+1];
            redirect = 1;
            cmd[idx] = NULL;
            break;
        }

        // 输入重定向
        if (**(cmd+idx) == '<') {
            out = cmd[idx+1];
            redirect = 0;
            cmd[idx] = NULL;
            break;
        }

        idx++;
    }

    // 执行命令
    if (inline_command(cmd, out, redirect)) return;
    else outer_command(cmd, out, redirect);

}

// 执行内建命令
int inline_command(char **cmd, char *out, int redirect) {

    int is_inline = 1;
    int old = -1;
    int fd = -1;

    if (redirect > -1) {
        // 复制一下要重定向的描述符
        old = dup(redirect);
    }

    if (redirect == 1) {
        // 输出重定向
        if ((fd = open(out, O_RDWR | O_CREAT, 0644)) == -1) {
            printf("redirect error\n");
            return 1;
        }
        // 清空文件
        ftruncate(fd, 0);
    } else if (redirect == 0) {
        // 输入重定向
        if ((fd = open(out, O_RDONLY)) == -1) {
            printf("redirect error\n");
            return 1;
        }
    }

    dup2(fd, redirect);
    close(fd);

    // 判断是否需要重定向，需要的话重定向
    char *command = cmd[0];

    // exit 退出终端
    if (strcmp(command, "exit") == 0) {
        // 退出前清理后台进程
        for (int i = 0; i < back_run.num; i++) {
            /* printf("kill pid %d\n", back_run.pids[i]); */
            kill(back_run.pids[i], SIGINT);
        }
        exit(0);
    } else if (strcmp(command, "cd") == 0) {
        chdir(cmd[1]);
    } else if (strcmp(command, "pwd") == 0) {
        char tmp_path[PATHLEN];
        getcwd(tmp_path, PATHLEN);
        printf("%s\n", tmp_path);
    } else if (strcmp(command, "bg") == 0) {
        // 后台运行
        outer_command(cmd+1, "/dev/null", -2);
    } else is_inline = 0;

    // 恢复重定向
    close(redirect);
    dup2(old, redirect);
    close(old);

    return is_inline;
}

// 执行外部命令
int outer_command(char **cmd, char *out, int redirect) {

    pid_t pid;
    int   status;
    int   fd;
    int   old;

    // 启动一个新的进程运行命令
    if ((pid = fork()) < 0)
        printf("fork error\n");

    else if (pid == 0) {
        if (redirect == 1) { // 输出重定向
            if ((fd = open(out, O_RDWR | O_CREAT, 0644)) == -1) {
                printf("redirect error\n");
                return 1;
            }
            // 清空文件
            ftruncate(fd, 0);
        } else if (redirect == 0) { // 输入重定向
            if ((fd = open(out, O_RDONLY)) == -1) {
                printf("redirect error\n");
                return 1;
            }
        } else if (redirect == -2) { // 后台运行
            if ((fd = open("/dev/null", O_RDWR)) == -1) {
                printf("redirect error\n");
                return 1;
            }
            // 将标准输入，输出，错误输出全部重定向到/dev/null
            dup2(fd, 0);
            dup2(fd, 1);
            dup2(fd, 2);
        }

        dup2(fd, redirect);
        close(fd);

        execvp(cmd[0], cmd);

        // 该进程没了
        // 不需要恢复重定向了，直接返回即可
        // close(redirect);
        // dup2(old, redirect);
        // close(old);
        exit(1);
    }

    if (redirect == -2) {
        // 只能增加，子进程退出的时候暂时没有检测
        if (back_run.num < BACK) {
            back_run.pids[back_run.num] = pid;
            back_run.num++;
        }
        printf("程序:%s 进程ID:%d 将在后台运行\n", cmd[0], pid);
        return 1;
    }

    current_run = pid;
    if ((pid = waitpid(pid, &status, 0)) < 0)
        printf("waitpid error\n");

    return 0;
}

void sig_int(int signo) {
    // 终止当前正在执行的函数
    if (current_run > 0) {
        kill(current_run, SIGINT);
        current_run = 0;
    }
}
