#include "lcd_display.h"
#include "sbc_lcd16x2.h"
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>

int lcd_display_frame(struct sample_lcd *lcd)
{
    size_t i;
    size_t pos;
    char c;
    int ret;

    if (lcd->message_len == 0) {
        ret = lcd_send_byte(lcd->client, 0x01, false);

        if (!ret)
            msleep(2);

        return ret;
    }

    /*
     * Set cursor to beginning of first line.
     */
    ret = lcd_send_byte(lcd->client, 0x80, false);
    if (ret)
        return ret;

    /*
     * Display first line.
     */
    for (i = 0; i < 16; i++) {

        pos = lcd->scroll_offset + i;

        c = (pos < lcd->message_len) ?
            lcd->message[pos] : ' ';

        ret = lcd_send_byte(lcd->client, c, true);
        if (ret)
            return ret;
    }

    /*
     * Set cursor to beginning of second line.
     */
    ret = lcd_send_byte(lcd->client, 0xC0, false);
    if (ret)
        return ret;

    /*
     * Display second line.
     */
    for (i = 0; i < 16; i++) {

        pos = lcd->scroll_offset + 16 + i;

        c = (pos < lcd->message_len) ?
            lcd->message[pos] : ' ';

        ret = lcd_send_byte(lcd->client, c, true);
        if (ret)
            return ret;
    }

    return 0;
}

void lcd_display_work(struct work_struct *work)
{
    struct sample_lcd *lcd;
    int ret;

    lcd = container_of(to_delayed_work(work),
                       struct sample_lcd, display_work);

    mutex_lock(&lcd->lock);

    if (lcd->disconnected) {
        mutex_unlock(&lcd->lock);
        return;
    }

    ret = lcd_display_frame(lcd);

    if (!ret && lcd->message_len > 32) {

        lcd->scroll_offset++;

        /*
         * Allow 16 trailing spaces before restarting.
         */
        if (lcd->scroll_offset >= lcd->message_len + 16)
            lcd->scroll_offset = 0;

        mod_delayed_work(system_dfl_wq,
                 &lcd->display_work,
                 msecs_to_jiffies(300));
    }

    mutex_unlock(&lcd->lock);

    if (ret)
        dev_err(&lcd->client->dev,
                "Failed to display LCD frame: %d\n", ret);
}