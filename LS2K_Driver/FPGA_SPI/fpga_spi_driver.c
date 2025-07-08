#include <linux/init.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

#define DEVICE_IP   	 0x16  // 设备识别符 
#define DEVICE_Set  	 0x3f  // 采样指令符 
#define DEVICE_Get  	 0xf6  // 读取指令符 
#define DEVICE_Div  	 0x74  // 分频指令符
#define SPI_SEPARATOR1_H 0x5a  // 分隔校验符1
#define SPI_SEPARATOR1_L 0xa5  // 分隔校验符1
#define SPI_SEPARATOR2_H 0x7b  // 分隔校验符2 
#define SPI_SEPARATOR2_L 0x89  // 分隔校验符2 
#define START_ID_H 		 0xaa  // 开始接收
#define START_ID_L 		 0x55  // 开始接收
#define END_ID_H 		 0xff  // 接收完毕
#define END_ID_L 		 0xee  // 接收完毕
#define ADC_DATA_SIZE	 2048  // byte for 1024 adc_data
ssize_t this_read(struct file *file, char __user *ubuf, size_t size, loff_t *lofft);
ssize_t this_write(struct file *file, const char __user *ubuf, size_t size, loff_t *lofft);
int this_open(struct inode *inode, struct file *file);
int this_close(struct inode *inode, struct file *file);
struct FPGA_SPI_cdev{
	dev_t devt;
	struct cdev cdev;
	struct file_operations fop;
	struct class* cls;
	struct device* dev;
};

struct FPGA_SPI_cdev fpga_spi_cdev = {
	.cdev = {
		.owner = THIS_MODULE
	},
	.fop = {
		.owner = THIS_MODULE,
		.open = this_open,
		.release = this_close,
		.read = this_read,
		.write = this_write
	}
};
	
struct spi_device *FPGA_SPI;
int	FPGA_SPI_probe(struct spi_device *spi)
{
	int ret = 0;
	printk("This is FPGA_SPI_probe\n");
	FPGA_SPI = spi;

	// 设置SPI模式，这里以模式0为例
	spi->max_speed_hz = 2000000;
	spi->mode = SPI_MODE_0;
	ret = spi_setup(spi);
	if (ret < 0) {
		printk("Failed to setup SPI device with mode 3\n");
		return ret;
	}
	
	ret = alloc_chrdev_region(&fpga_spi_cdev.devt, 0, 1, "FPGA_SPI");
	if(ret!=0){
		printk("alloc_chrdev_region error\n");
		goto error_alloc_chrdev_region;
	}
	cdev_init(&fpga_spi_cdev.cdev, &fpga_spi_cdev.fop);
	ret = cdev_add(&fpga_spi_cdev.cdev, fpga_spi_cdev.devt, 1);
	if(ret!=0){
		printk("cdev_add error\n");
		goto error_cdev_add;
	}
	fpga_spi_cdev.cls = class_create(THIS_MODULE, "FPGA_SPI");
	if(!fpga_spi_cdev.cls){
		printk("class_create error\n");
		goto error_class_create;
	}
	fpga_spi_cdev.dev = device_create(fpga_spi_cdev.cls,NULL,fpga_spi_cdev.devt,NULL,"FPGA_SPI_dev");
	if(!fpga_spi_cdev.dev){
		printk("device_create error\n");
		goto error_device_create;
	}
	return 0;
	error_device_create:
		device_destroy(fpga_spi_cdev.cls, fpga_spi_cdev.devt);
	error_class_create:
		class_destroy(fpga_spi_cdev.cls);
	error_cdev_add:
		cdev_del(&fpga_spi_cdev.cdev);
	error_alloc_chrdev_region:
		unregister_chrdev_region(fpga_spi_cdev.devt, 1);
	return ret;
}

int	FPGA_SPI_remove(struct spi_device *spi)
{
	device_destroy(fpga_spi_cdev.cls, fpga_spi_cdev.devt);
	class_destroy(fpga_spi_cdev.cls);
	cdev_del(&fpga_spi_cdev.cdev);
	unregister_chrdev_region(fpga_spi_cdev.devt, 1);
	return 0;
}

const struct of_device_id FPGA_SPI_match_table[] = {
	{.compatible = "FPGA_SPI"},
	{}
};
struct spi_driver FPGA_SPI_drv = {
	.driver = {
		.name = "FPGA_SPI",
		.owner = THIS_MODULE,
		.of_match_table = FPGA_SPI_match_table
	},
	.probe = FPGA_SPI_probe,
	.remove = FPGA_SPI_remove
};


static int FPGA_SPI_init(void)
{
	int ret;
	ret = spi_register_driver(&FPGA_SPI_drv);
	return ret;
}

static void FPGA_SPI_exit(void)
{
	spi_unregister_driver(&FPGA_SPI_drv);
}

module_init(FPGA_SPI_init);
module_exit(FPGA_SPI_exit);
MODULE_LICENSE("GPL");

bool FPGA_ADC_set(void)
{
	int ret = 0;
	char set_buf[2] = {DEVICE_IP, DEVICE_Set};
	char end_buf[2];
	// 发送设备识别码并接收数据
    ret = spi_write_then_read(FPGA_SPI, set_buf, 2, end_buf, 2);
    if (ret != 0) {
        printk("Failed to perform write then read operation\n");
        return false;
    }

	// 校验结束校验码
    if ((uint8_t)end_buf[0] != END_ID_H || (uint8_t)end_buf[1] != END_ID_L) {
        printk("End check code error\n");
        return false;
    }

	return true;
}

ssize_t this_read(struct file *file, char __user *ubuf, size_t size, loff_t *lofft)
{
	int ret = 0;
	char id_buf[2] = {DEVICE_IP, DEVICE_Get};
    char *re_buf = kmalloc(2 + ADC_DATA_SIZE + 2 + ADC_DATA_SIZE + 2 + ADC_DATA_SIZE + 2, GFP_KERNEL);
    if (!re_buf) {
        printk("Failed to allocate memory for re_buf\n");
        return -ENOMEM;
    }

	if(FPGA_ADC_set()){
		printk("FPGA_ADC_set Successfully\n");
	}else{
		printk("FPGA_ADC_set error\n");
		kfree(re_buf);
		return -1;
	}
    // 发送设备识别码并接收数据
    ret = spi_write_then_read(FPGA_SPI, id_buf, 2, re_buf, 2 + ADC_DATA_SIZE + 2 + ADC_DATA_SIZE + 2 + ADC_DATA_SIZE + 2);
    if (ret != 0) {
        printk("Failed to perform write then read operation\n");
        kfree(re_buf);
        return ret;
    }

	printk("%x,%x\n",(uint8_t)re_buf[0],(uint8_t)re_buf[1]);
	printk("%x,%x\n",(uint8_t)re_buf[2+ADC_DATA_SIZE],(uint8_t)re_buf[2+ADC_DATA_SIZE+1]);
	printk("%x,%x\n",(uint8_t)re_buf[2+ADC_DATA_SIZE+2+ADC_DATA_SIZE],(uint8_t)re_buf[2+ADC_DATA_SIZE+2+ADC_DATA_SIZE+1]);
	printk("%x,%x\n",(uint8_t)re_buf[2+ADC_DATA_SIZE+2+ADC_DATA_SIZE+2+ADC_DATA_SIZE],(uint8_t)re_buf[2+ADC_DATA_SIZE+2+ADC_DATA_SIZE+2+ADC_DATA_SIZE+1]);
	
    // 校验开始校验码
    if ((uint8_t)re_buf[0] != START_ID_H || (uint8_t)re_buf[1] != START_ID_L) {
        printk("Start check code error\n");
        kfree(re_buf);
        return -EINVAL;
    }

    // 校验分隔校验码1
    if ((uint8_t)re_buf[2 + ADC_DATA_SIZE] != SPI_SEPARATOR1_H || (uint8_t)re_buf[2 + ADC_DATA_SIZE + 1] != SPI_SEPARATOR1_L) {
        printk("Separator1 check code error\n");
        kfree(re_buf);
        return -EINVAL;
    }

	// 校验分隔校验码2
    if ((uint8_t)re_buf[2 + ADC_DATA_SIZE + 2 + ADC_DATA_SIZE] != SPI_SEPARATOR2_H || (uint8_t)re_buf[2 + ADC_DATA_SIZE + 2 + ADC_DATA_SIZE + 1] != SPI_SEPARATOR2_L) {
        printk("Separator2 check code error\n");
        kfree(re_buf);
        return -EINVAL;
    }

    // 校验结束校验码
    if ((uint8_t)re_buf[2 + ADC_DATA_SIZE + 2 + ADC_DATA_SIZE + 2 + ADC_DATA_SIZE] != END_ID_H || (uint8_t)re_buf[2 + ADC_DATA_SIZE + 2 + ADC_DATA_SIZE+ 2 + ADC_DATA_SIZE + 1] != END_ID_L) {
        printk("End check code error\n");
        kfree(re_buf);
        return -EINVAL;
    }
	
    ret = copy_to_user(ubuf, re_buf + 2, ADC_DATA_SIZE * 3 + 2 * 2);
    if (ret != 0) {
        printk("Failed to copy_to_user\n");
        kfree(re_buf);
        return ret;
    }
    kfree(re_buf);
    return ADC_DATA_SIZE * 3 + 2 * 2;
}

ssize_t this_write(struct file *file, const char __user *ubuf, size_t size, loff_t *lofft)
{
    int ret = 0;
    char *wr_buf = kmalloc(size, GFP_KERNEL);
    if (!wr_buf) {
        printk("Failed to allocate memory for wr_buf\n");
        return -ENOMEM;
    }
    ret = copy_from_user(wr_buf, ubuf, size);
    if(ret != 0)
    {
        printk("Failed to copy_from_user\n");
        kfree(wr_buf);
        return ret;
    }
    ret = spi_write(FPGA_SPI, wr_buf, size);
    if(ret != 0)
    {
        printk("Failed to spi_write\n");
        kfree(wr_buf);
        return ret;
    }
    kfree(wr_buf);
    return size;
}

int this_open(struct inode *inode, struct file *file)
{
	printk("open FPGA_SPI_dev called\n");
	return 0;
}
int this_close(struct inode *inode, struct file *file)
{
	printk("close FPGA_SPI_dev called\n");
	return 0;
}
