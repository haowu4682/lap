## For stock installed kernel
KVER := $(shell uname -r)

## For kernel build tree
KDIR := /lib/modules/$(KVER)/build
#KDIR := ../../linux-3.11.8/

PWD  := `pwd`

COMM_KHDRS := common.h
LAP_KHDRS := $(COMM_KHDRS)
LAP_KSRCS := lap.c
LAP_KOBJS := $(LAP_KSRCS:.c=.o)

obj-m := lap.o
#lap-objs := $(LAP_KOBJS)

all := lap.ko
all: $(all)

lap.ko: $(LAP_KSRCS) $(LAP_KHDRS)
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f $(all) *.o

.PHONY: all clean
