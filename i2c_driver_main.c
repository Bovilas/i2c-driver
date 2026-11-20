/*
matching device tree node:
// custom i2c driver //
&i2c1 {
	status = "okay";
	clock-frequency = <100000>;
	lcd@27 {
	    compatible = "vendor,example-device";
	    reg = <0x27>;
	    status = "okay";
	};
};
*/
// i2c_driver_main.c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/string.h>

#include "pcf8574.h"
#include "sbc_lcd16x2.h"
#include "lcd_display.h" // sample_lcd struct definition included here

#define LCD_IOC_CLEAR _IO('L', 0)

static void lcd_validate_message(char *buffer, size_t length);
static void lcd_release(struct kref *ref);
static int lcd_clear_message(struct sample_lcd *lcd);
static void i2c_driver_remove(struct i2c_client *client);
static int sample_i2c_probe(struct i2c_client *client);
static ssize_t lcd_write(struct file *file, const char __user *user_buffer, size_t count, loff_t *ppos);
static const struct file_operations lcd_fops;
static const struct of_device_id i2c_driver_of_match[];
static const struct i2c_device_id i2c_driver_id_table[];
static struct i2c_driver i2c_driver;
static long lcd_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int lcd_open(struct inode *inode, struct file *file);
static int lcd_release_file(struct inode *inode, struct file *file);

static void lcd_validate_message(char *buffer, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++) {

        unsigned char c = buffer[i];

        if (c < 0x20 || c > 0x7E)
            buffer[i] = '?';
    }
}

static void lcd_release(struct kref *ref)
{
    struct sample_lcd *lcd;

    lcd = container_of(ref, struct sample_lcd, refcount);

    kfree(lcd);
}

static int lcd_clear_message(struct sample_lcd *lcd)
{
    int ret;

    mutex_lock(&lcd->lock);

    if (lcd->disconnected) {
        mutex_unlock(&lcd->lock);
        return -ENODEV;
    }

    lcd->message[0] = '\0';
    lcd->message_len = 0;
    lcd->scroll_offset = 0;

    ret = lcd_send_byte(lcd->client, 0x01, false);

    if (!ret)
        msleep(2);

    mutex_unlock(&lcd->lock);

    return ret;
}

static ssize_t lcd_write(struct file *file,
                         const char __user *user_buffer,
                         size_t count,
                         loff_t *ppos)
{
    struct sample_lcd *lcd = file->private_data;
    char *buffer;
    size_t length;
    int ret;

    (void)ppos;

    if (count == 0)
        return 0;

    if (count > LCD_MAX_WRITE)
        return -EMSGSIZE;

    /* Copy message from userspace */
    buffer = memdup_user_nul(user_buffer, count);
    if (IS_ERR(buffer))
        return PTR_ERR(buffer);

    if (memchr(buffer, '\0', count)) {
        kfree(buffer);
        return -EINVAL;
    }

    length = count;

    /* Remove trailing newline characters */
    while (length > 0 &&
           (buffer[length - 1] == '\n' ||
            buffer[length - 1] == '\r')) {
        length--;
    }

    buffer[length] = '\0';

    lcd_validate_message(buffer, length);

    /*
     * Serialize userspace write/clear commands.
     * This mutex must also be initialized in probe().
     */
    mutex_lock(&lcd->command_lock);

    /* Stop the previous message rotation */
    cancel_delayed_work_sync(&lcd->display_work);

    /* Check whether userspace requested a clear */
    if (length == 5 && !memcmp(buffer, "clear", 5)) {
        ret = lcd_clear_message(lcd);

        if (!ret)
            pr_info("i2c_driver: LCD cleared\n");

        mutex_unlock(&lcd->command_lock);
        kfree(buffer);

        return ret ? ret : count;
    }

    /* Store the new message */
    mutex_lock(&lcd->lock);

    if (lcd->disconnected) {
        mutex_unlock(&lcd->lock);
        mutex_unlock(&lcd->command_lock);
        kfree(buffer);
        return -ENODEV;
    }

    memcpy(lcd->message, buffer, length);
    lcd->message[length] = '\0';

    lcd->message_len = length;
    lcd->scroll_offset = 0;

    mutex_unlock(&lcd->lock);

    kfree(buffer);

    /* Start displaying the new message immediately */
    mod_delayed_work(system_dfl_wq, &lcd->display_work, 0);

    mutex_unlock(&lcd->command_lock);

    return count;
}
static long lcd_ioctl(struct file *file,
                      unsigned int cmd,
                      unsigned long arg)
{
    struct sample_lcd *lcd = file->private_data;
    int ret;

    if (cmd != LCD_IOC_CLEAR)
        return -ENOTTY;

    mutex_lock(&lcd->command_lock);

    mutex_lock(&lcd->lock);

    if (lcd->disconnected) {
        mutex_unlock(&lcd->lock);
        mutex_unlock(&lcd->command_lock);
        return -ENODEV;
    }

    mutex_unlock(&lcd->lock);

    cancel_delayed_work_sync(&lcd->display_work);

    ret = lcd_clear_message(lcd);

    mutex_unlock(&lcd->command_lock);

    return ret;
}


static const struct file_operations lcd_fops = {
    .owner          = THIS_MODULE,
    .open           = lcd_open,
    .release        = lcd_release_file,
    .write          = lcd_write,
    .unlocked_ioctl = lcd_ioctl,
};

static int lcd_open(struct inode *inode, struct file *file)
{
    struct miscdevice *miscdev = file->private_data;
    struct sample_lcd *lcd;

    lcd = container_of(miscdev, struct sample_lcd, miscdev);

    mutex_lock(&lcd->lock);

    if (lcd->disconnected) {
        mutex_unlock(&lcd->lock);
        return -ENODEV;
    }

    kref_get(&lcd->refcount);

    mutex_unlock(&lcd->lock);

    file->private_data = lcd;

    return 0;
}

static int lcd_release_file(struct inode *inode, struct file *file)
{
    struct sample_lcd *lcd = file->private_data;

    kref_put(&lcd->refcount, lcd_release);

    return 0;
}

static int sample_i2c_probe(struct i2c_client *client)
{
    struct sample_lcd *lcd;
    int ret;

    dev_info(&client->dev,
             "Probe called! Address: 0x%02x\n",
             client->addr);

    /* Check I2C functionality */
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "Plain I2C not supported\n");
        return -EOPNOTSUPP;
    }

    /* Allocate device structure */
    lcd = kzalloc(sizeof(*lcd), GFP_KERNEL);

    if (!lcd)
        return -ENOMEM;

    kref_init(&lcd->refcount);

    lcd->client = client;

    /* Initialize mutex */
    mutex_init(&lcd->lock);
    mutex_init(&lcd->command_lock);

    /* Initialize background display work */
    INIT_DELAYED_WORK(&lcd->display_work, lcd_display_work);

    /* Initialize LCD hardware */
    ret = initialize_lcd(client);

    if (ret) {
        dev_err(&client->dev,
                "LCD initialization failed: %d\n", ret);

        kref_put(&lcd->refcount, lcd_release);
        return ret;
    }

    /* Configure character device */
    lcd->miscdev.minor = MISC_DYNAMIC_MINOR;
    lcd->miscdev.name = "lcd";
    lcd->miscdev.fops = &lcd_fops;
    lcd->miscdev.parent = &client->dev;
    lcd->miscdev.mode = 0660;

    /* Register character device */
    ret = misc_register(&lcd->miscdev);

    if (ret) {
        dev_err(&client->dev,
                "Failed to register LCD character device: %d\n",
                ret);
        kref_put(&lcd->refcount, lcd_release);
        return ret;
    }

    i2c_set_clientdata(client, lcd);


    dev_info(&client->dev,
             "LCD character device registered: /dev/lcd\n");

    return 0;
}

static void i2c_driver_remove(struct i2c_client *client)
{
    struct sample_lcd *lcd = i2c_get_clientdata(client);

    dev_info(&client->dev, "Removing LCD device\n");

    /* Prevent new file descriptors from being opened */
    misc_deregister(&lcd->miscdev);

    mutex_lock(&lcd->command_lock);

    /* Mark device as disconnected */
    mutex_lock(&lcd->lock);
    lcd->disconnected = true;
    mutex_unlock(&lcd->lock);

    /* Stop background display operations */
    cancel_delayed_work_sync(&lcd->display_work);

    mutex_unlock(&lcd->command_lock);

    dev_info(&client->dev, "LCD device removed\n");

    /* Release driver's reference */
    kref_put(&lcd->refcount, lcd_release);
}

static const struct of_device_id i2c_driver_of_match[] = {
	{ .compatible = "vendor,example-device" },
	{  }
};
MODULE_DEVICE_TABLE(of, i2c_driver_of_match);

static const struct i2c_device_id i2c_driver_id_table[] = {
	{ "example-device", 0 },
	{  }
};

MODULE_DEVICE_TABLE(i2c, i2c_driver_id_table);

static struct i2c_driver i2c_driver = {
	.driver = {
		.name = "i2c-driver",
		.of_match_table = i2c_driver_of_match,
	},
	.probe = sample_i2c_probe,
	.remove = i2c_driver_remove,
	.id_table = i2c_driver_id_table,
};

MODULE_DESCRIPTION("Linux I2C driver");
MODULE_AUTHOR("povilas.daugela@teltonika.lt");
MODULE_LICENSE("GPL");
MODULE_VERSION("1");

module_i2c_driver(i2c_driver);