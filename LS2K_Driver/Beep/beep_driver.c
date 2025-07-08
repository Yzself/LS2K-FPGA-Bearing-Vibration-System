#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h> 
#include <linux/miscdevice.h>      
#include <linux/fs.h>              
#include <linux/gpio/consumer.h>   
#include <linux/of.h>              
#include <linux/slab.h>            
#include <linux/uaccess.h>         
#include <linux/ioctl.h>           

// --- IOCTL 命令定义 ---
#define BEEP_MAGIC   'B'
#define BEEP_OFF     _IO(BEEP_MAGIC, 0)
#define BEEP_ON      _IO(BEEP_MAGIC, 1)


// --- 设备私有数据结构 ---
struct beep_dev {
    struct gpio_desc *gpiod;      // GPIO描述符
    struct miscdevice misc_dev;   // 杂项设备结构体
};


// --- 文件操作函数集 (file_operations) ---
static long beep_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    // 从file结构体中获取miscdevice，再从中获取我们的私有数据
    struct miscdevice *miscdev = file->private_data;
    struct beep_dev *dev = container_of(miscdev, struct beep_dev, misc_dev);

    // 根据命令执行操作
    switch (cmd) {
    case BEEP_ON:
        gpiod_set_value(dev->gpiod, 1); // 设置GPIO为高电平
        pr_info("Beep ON\n");
        break;
    case BEEP_OFF:
        gpiod_set_value(dev->gpiod, 0); // 设置GPIO为低电平
        pr_info("Beep OFF\n");
        break;
    default:
        pr_warn("Invalid ioctl command\n");
        return -EINVAL; // 无效参数
    }

    return 0;
}

// 定义与杂项设备关联的文件操作
static const struct file_operations beep_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = beep_ioctl,
};


// --- 平台驱动函数集 (platform_driver) ---
static int beep_probe(struct platform_device *pdev)
{
	printk("This is beep_probe.\n");
    struct device *dev = &pdev->dev;
    struct beep_dev *beep_priv;
    int ret;

    // 1. 分配私有数据结构内存 (使用devm_kzalloc，内核自动管理)
    beep_priv = devm_kzalloc(dev, sizeof(struct beep_dev), GFP_KERNEL);
    if (!beep_priv) {
        return -ENOMEM;
    }

    // 2. 从设备树获取GPIO (使用devm_gpiod_get，内核自动管理)
    beep_priv->gpiod = devm_gpiod_get(dev, "beep", GPIOD_OUT_LOW);
    if (IS_ERR(beep_priv->gpiod)) {
        ret = PTR_ERR(beep_priv->gpiod);
        dev_err(dev, "Failed to get beep-gpios: %d\n", ret);
        return ret;
    }

    // 3. 初始化杂项设备
    beep_priv->misc_dev.minor = MISC_DYNAMIC_MINOR; // 动态分配次设备号
    beep_priv->misc_dev.name = "crazy_beep";        // 设备节点名 /dev/crazy_beep
    beep_priv->misc_dev.fops = &beep_fops;          // 关联文件操作

    // 4. 注册杂项设备
    ret = misc_register(&beep_priv->misc_dev);
    if (ret) {
        dev_err(dev, "Failed to register misc device\n");
        // devm_* 分配的资源会自动释放，无需手动处理
        return ret;
    }

    // 5. 将私有数据保存到平台设备中，以便在remove时使用
    platform_set_drvdata(pdev, beep_priv);

    dev_info(dev, "Crazy Beep driver probed successfully!\n");
    return 0;
}

static int beep_remove(struct platform_device *pdev)
{
    // 获取probe时保存的私有数据
    struct beep_dev *beep_priv = platform_get_drvdata(pdev);
    
    // 确保蜂鸣器在卸载前是关闭的
    gpiod_set_value(beep_priv->gpiod, 0);

    // 1. 注销杂项设备
    misc_deregister(&beep_priv->misc_dev);
    
    // 2. GPIO和内存资源由devm_*管理，会自动释放，无需手动gpiod_put或kfree
    dev_info(&pdev->dev, "Crazy Beep driver removed.\n");
    return 0;
}

static const struct of_device_id beep_of_match[] = {
    { .compatible = "BEEP_GPIO" },
    {}
};
MODULE_DEVICE_TABLE(of, beep_of_match);

// 定义平台驱动结构体
static struct platform_driver beep_platform_driver = {
    .driver = {
		.owner = THIS_MODULE,
        .name = "crazy_beep_gpio",
        .of_match_table = of_match_ptr(beep_of_match),
    },
    .probe = beep_probe,
    .remove = beep_remove,
};


// --- 模块初始化与退出 ---
module_platform_driver(beep_platform_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A simple platform driver for a beep controlled by GPIO via ioctl");




