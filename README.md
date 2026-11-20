# i2c-driver

I2C Driver for LCD 16x2 screen

## Usage

make KDIR=/path/to/kernel/dir ARCH=arm64 CROSS_COMPILE=/path/to/gnu/aarch64-rpi4-linux-gnu-
make KDIR=/path/to/kernel/dir ARCH=arm64 CROSS_COMPILE=/path/to/gnu/aarch64-rpi4-linux-gnu- clean
insmod i2c_driver.ko
echo "0000000000000000111111111111111122222222222222223333333333333" > /dev/lcd
echo "clear" > /dev/lcd
echo "hello" > /dev/lcd
