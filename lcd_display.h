#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/kref.h>

#define LCD_MAX_WRITE 4096

struct sample_lcd {
    struct i2c_client *client;

    struct kref refcount;

    /*
     * Protects:
     * - message
     * - message_len
     * - page_offset
     * - disconnected
     * - All LCD hardware operations
     */
    struct mutex lock;

    /*
     * Serializes userspace commands.
     * Must be acquired before lock.
     */
    struct mutex command_lock;

    struct miscdevice miscdev;
    struct delayed_work display_work;

    char message[LCD_MAX_WRITE + 1];
    size_t message_len;
    size_t scroll_offset;

    bool disconnected;
};

int lcd_display_frame(struct sample_lcd *lcd);
void lcd_display_work(struct work_struct *work);

#endif /* LCD_DISPLAY_H */