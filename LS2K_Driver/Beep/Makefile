obj-m += beep_driver.o

export ARCH=loongarch
export CROSS_COMPILE=loongarch64-linux-gnu-

KDIR:=/home/yzself/Desktop/uisrc-lab-loongarch/sources/kernel

PWD?=$(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules
	rm -rf *.symvers *.order *.o *.mod.o *.mod.c
