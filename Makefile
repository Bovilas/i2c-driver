obj-m += i2c_driver.o

i2c_driver-y := i2c_driver_main.o \
                lcd_display.o \
                sbc_lcd16x2.o \
                pcf8574.o

KDIR ?= /lib/modules/$(shell uname -r)/build
ARCH ?=
CROSS_COMPILE ?=

PWD := $(CURDIR)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		W=1 modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean