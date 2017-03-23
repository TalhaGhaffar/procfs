obj-m += p4fs.o

#KDIR := /home/user/Desktop/pfs_project/linux/
KDIR := /home/user/Desktop/VM/linux-4.0.9/


PWD := $(shell pwd)

#CFLAFS_p4fs.o += -O0
#ccflags-y := -O2

all: p4fs.c
	make -C $(KDIR) M=$(PWD) V=1 modules

modules_install:
	make -C $(KDIR) M=$(PWD) modules_install

clean:
	make -C $(KDIR) M=$(PWD) clean
