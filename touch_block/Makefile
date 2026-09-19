obj-m += touch_block.o

# 注意：KDIR 需要根据你实际编译环境的内核源码路径进行修改
# 如果你使用 GitHub Actions 云端编译，Makefile 通常由云端脚本自动处理，
# 或者保留此文件并在 YAML 中指定 KDIR。
KDIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
