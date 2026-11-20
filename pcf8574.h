#ifndef PCF8574_H
#define PCF8574_H

#include <linux/i2c.h>
#include <linux/types.h>

int pcf8574_write(struct i2c_client *client, u8 value);

#endif /* PCF8574_H */