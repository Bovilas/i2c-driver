#ifndef SBC_LCD16X2_H
#define SBC_LCD16X2_H

#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/device.h>

#define LCD_RS  0x01
#define LCD_EN  0x04
#define LCD_BL  0x08

int lcd_send_byte(struct i2c_client *client, u8 value, bool rs);

int lcd_write_nibble(struct i2c_client *client,
                     u8 nibble, bool rs);

int initialize_lcd(struct i2c_client *client);

#endif