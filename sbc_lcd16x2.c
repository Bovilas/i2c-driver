#include "sbc_lcd16x2.h"
#include <linux/delay.h>  // udelay(), usleep_range(), msleep()
#include "pcf8574.h"      // pcf8574_write()

#define LCD_INIT_CHECK(_operation, _call)       \
    do {                                        \
        ret = (_call);                          \
        if (ret) {                              \
            dev_err(&client->dev,               \
                    "LCD init failed at %s: %d\n", \
                    _operation, ret);           \
            return ret;                         \
        }                                       \
    } while (0)

int lcd_write_nibble(struct i2c_client *client, u8 nibble, bool rs) {
    u8 data;
    int ret;

    data = (nibble << 4) | LCD_BL;

    if (rs)
        data |= LCD_RS;

    /* Enable HIGH */
    ret = pcf8574_write(client, data | LCD_EN);
    if (ret)
        return ret;

    udelay(1);

    /* Enable LOW: LCD reads the nibble */
    ret = pcf8574_write(client, data);
    if (ret)
        return ret;

    usleep_range(40, 50);

    return 0;
}

int lcd_send_byte(struct i2c_client *client,
                         u8 value, bool rs)
{
    int ret;

    /* Send upper 4 bits */
    ret = lcd_write_nibble(client, value >> 4, rs);
    if (ret)
        return ret;

    /* Send lower 4 bits */
    ret = lcd_write_nibble(client, value & 0x0F, rs);
    if (ret)
        return ret;

    return 0;
}

int initialize_lcd(struct i2c_client *client)
{
    int ret;

    msleep(40);

    LCD_INIT_CHECK("startup nibble 1 (0x03)",
                   lcd_write_nibble(client, 0x03, false));

    msleep(5);

    LCD_INIT_CHECK("startup nibble 2 (0x03)",
                   lcd_write_nibble(client, 0x03, false));

    usleep_range(100, 150);

    LCD_INIT_CHECK("startup nibble 3 (0x03)",
                   lcd_write_nibble(client, 0x03, false));

    usleep_range(100, 150);

    LCD_INIT_CHECK("switch to 4-bit mode (0x02)",
                   lcd_write_nibble(client, 0x02, false));

    /* Configure LCD */
    LCD_INIT_CHECK("function set (0x28)",
            lcd_send_byte(client, 0x28, false));

        
    LCD_INIT_CHECK("display off (0x08)",
               lcd_send_byte(client, 0x08, false));

    LCD_INIT_CHECK("clear display (0x01)",
                   lcd_send_byte(client, 0x01, false));

    msleep(2);


    LCD_INIT_CHECK("entry mode (0x06)",
                   lcd_send_byte(client, 0x06, false));

    LCD_INIT_CHECK("display on (0x0C)",
                   lcd_send_byte(client, 0x0C, false));

    return 0;
}
