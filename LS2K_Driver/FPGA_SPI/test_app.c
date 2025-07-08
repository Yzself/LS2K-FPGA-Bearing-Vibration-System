#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEVICE_NAME "/dev/FPGA_SPI_dev"
#define ADC_DATA_SIZE 2048
#define BUFFER_SIZE (ADC_DATA_SIZE * 3 + 2 * 2)
int x_adc[ADC_DATA_SIZE / 2];
int y_adc[ADC_DATA_SIZE / 2];
int z_adc[ADC_DATA_SIZE / 2];

int main(int argc, char* argv[]) {
    int fd;
    char read_buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // 打开设备文件
    fd = open(DEVICE_NAME, O_RDWR);
    if (fd == -1) {
        perror("Failed to open device");
        return -1;
    }

    // 读取数据
    bytes_read = read(fd, read_buffer, BUFFER_SIZE);
    if (bytes_read == -1) {
        perror("Failed to read from device");
        close(fd);
        return -1;
    }

    // 检查读取的数据长度是否符合预期
    if (bytes_read != BUFFER_SIZE) {
        fprintf(stderr, "Read data length does not match expected size.\n");
        close(fd);
        return -1;
    }
	for (int i = 0; i < ADC_DATA_SIZE / 2; i++) {
		x_adc[i] = ((unsigned char)read_buffer[2 * i] << 2) | (unsigned char)read_buffer[2 * i + 1];
		y_adc[i] = ((unsigned char)read_buffer[ADC_DATA_SIZE + 2 + 2 * i] << 2) | (unsigned char)read_buffer[ADC_DATA_SIZE + 2 + 2 * i + 1];
		z_adc[i] = ((unsigned char)read_buffer[(ADC_DATA_SIZE + 2) * 2 + 2 * i] << 2) | (unsigned char)read_buffer[(ADC_DATA_SIZE + 2) * 2 + 2 * i + 1];
		printf("%d,%d,%d\n",x_adc[i],y_adc[i],z_adc[i]);
	}
	
    // 关闭设备文件
    close(fd);

    return 0;
}    

