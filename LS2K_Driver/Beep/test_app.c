#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

// --- IOCTL 命令定义 ---
#define BEEP_MAGIC   'B'
#define BEEP_OFF     _IO(BEEP_MAGIC, 0)
#define BEEP_ON      _IO(BEEP_MAGIC, 1)

// 设备节点路径
#define DEVICE_PATH "/dev/crazy_beep"

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s <on|off>\n", prog_name);
}

int main(int argc, char *argv[]) {
    int fd;
    int cmd;

    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "on") == 0) {
        cmd = BEEP_ON;
    } else if (strcmp(argv[1], "off") == 0) {
        cmd = BEEP_OFF;
    } else {
        fprintf(stderr, "Error: Invalid argument '%s'\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }

    // 打开设备文件
    fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open " DEVICE_PATH);
        return 1;
    }

    // 发送IOCTL命令
    printf("Sending command: %s\n", argv[1]);
    if (ioctl(fd, cmd, 0) < 0) {
        perror("ioctl failed");
        close(fd);
        return 1;
    }
    
    printf("Command sent successfully.\n");

    // 关闭设备文件
    close(fd);
    return 0;
}

