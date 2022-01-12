/*
 * max929x.c - max929x IO Expander driver
 *
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
/* #define DEBUG */

#define DEBUG

#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <media/camera_common.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include "max929x.h"

struct max929x {
	struct i2c_client *i2c_client;
	struct regmap *regmap;
	struct gpio_desc *pwdn_gpio;
};
struct max929x *priv;

static int max929x_write_reg(u8 slave_addr, u16 reg, u8 val)
{
	struct i2c_client *i2c_client = priv->i2c_client;
	int err;

	i2c_client->addr = slave_addr;
	err = regmap_write(priv->regmap, reg, val);
	if (err)
		dev_err(&i2c_client->dev, "%s:i2c write failed, slave_addr 0x%x, 0x%x = 0x%x\n",
			__func__, slave_addr, reg, val);

	return err;
}
/*
static int max929x_read_reg(u8 slave_addr, u16 reg, unsigned int *val)
{
	struct i2c_client *i2c_client = priv->i2c_client;
	int err;

	i2c_client->addr = slave_addr;
	err = regmap_read(priv->regmap, reg, val);
	if (err)
		dev_err(&i2c_client->dev, "%s:i2c read failed, 0x%x = %x\n",
			__func__, reg, *val);

	return err;
}
 */
int max929x_write_reg_list(struct max929x_reg *table, int size)
{
	struct device dev = priv->i2c_client->dev;
	int err = 0, i;
	u8 slave_addr;
	u16 reg;
	u8 val;

	for(i=0; i<size; i++)
	{
		slave_addr = table[i].slave_addr;
		reg = table[i].reg;
		val = table[i].val;

		dev_dbg(&dev, "%s: size %d, slave_addr 0x%x, reg 0x%x, val 0x%x\n",
				__func__, size, slave_addr, reg, val);

		err = max929x_write_reg(slave_addr, reg, val);

		if(err!=0)
			break;

		if (reg == 0x0010 || reg == 0x0000)
		    msleep(300);
	}

	return err;
}

static  struct regmap_config max929x_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
};

static int max929x_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device dev = client->dev;
	int err;

	dev_dbg(&dev, "%s: enter\n", __func__);

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	priv->i2c_client = client;
	priv->regmap = devm_regmap_init_i2c(priv->i2c_client, &max929x_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(priv->regmap));
		return -ENODEV;
	}

	priv->pwdn_gpio = devm_gpiod_get(&client->dev, "pwdn", GPIOD_OUT_HIGH);
	gpiod_set_value(priv->pwdn_gpio, false);

	err = max929x_write_reg_list(max929x_Double_Dser_Ser_init,
			sizeof(max929x_Double_Dser_Ser_init)/sizeof(struct max929x_reg));
	if(err == 0)
		dev_dbg(&dev, "%s: success\n", __func__);
	else
		dev_err(&dev, "%s: fail\n", __func__);

	return 0;
}

static int max929x_remove(struct i2c_client *client)
{
	struct device dev = client->dev;

	dev_dbg(&dev, "%s: \n", __func__);

	return 0;
}

static const struct i2c_device_id max929x_id[] = {
	{ "max929x", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, max929x_id);

const struct of_device_id max929x_of_match[] = {
	{ .compatible = "nvidia,max929x", },
	{ },
};
MODULE_DEVICE_TABLE(of, imx185_of_match);

static struct i2c_driver max929x_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "max929x",
		.of_match_table = of_match_ptr(max929x_of_match),
	},
	.probe = max929x_probe,
	.remove = max929x_remove,
	.id_table = max929x_id,
};

static int __init max929x_init(void)
{
	return i2c_add_driver(&max929x_i2c_driver);
}

static void __exit max929x_exit(void)
{
	i2c_del_driver(&max929x_i2c_driver);
}

module_init(max929x_init);
module_exit(max929x_exit);

MODULE_DESCRIPTION("IO Expander driver max929x");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL v2");
