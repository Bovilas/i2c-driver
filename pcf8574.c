#include "pcf8574.h"
#include <linux/errno.h>

int pcf8574_write(struct i2c_client *client, u8 value)
{
    int ret;

    ret = i2c_master_send(client, (const char *)&value, 1);

    if (ret < 0)
        return ret;

    if (ret != 1)
        return -EIO;

    return 0;
}