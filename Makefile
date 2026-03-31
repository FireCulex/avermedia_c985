# SPDX-License-Identifier: GPL-2.0

MODULE_NAME := avermedia_c985_poc

obj-m := $(MODULE_NAME).o

$(MODULE_NAME)-objs := \
	avermedia_c985.o \
	cqlcodec.o \
	cpr.o \
	i2c_bitbang.o \
	ql201_i2c.o \
	ti3101.o \
	nuc100.o \
	diag.o \
	project.o \
	qphci.o \
	qpfwapi.o \
	qpfwencapi.o \
	encoder.o \
	pciecntl.o \
	v4l2.o \
	interrupts.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) LLVM=1 modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) LLVM=1 clean

load: all
	-sudo rmmod $(MODULE_NAME) 2>/dev/null || true
	sudo insmod $(MODULE_NAME).ko
	sudo dmesg | tail -50

unload:
	-sudo rmmod $(MODULE_NAME) 2>/dev/null || true

reload: unload load

log:
	sudo dmesg | grep -i avermedia | tail -100

watch:
	sudo dmesg -wH | grep -i --line-buffered avermedia

.PHONY: all clean load unload reload log watch
