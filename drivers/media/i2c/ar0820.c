/*
 * ar0820.c - ar0820 sensor driver
 *
 * Copyright (c) 2015-2019, NVIDIA CORPORATION.  All rights reserved.
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
#define DEBUG 

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include <media/tegra_v4l2_camera.h>
#include <media/tegracam_core.h>
#include <media/ar0820.h>
#include <media/max9295.h>
#include <media/max9296.h>

#include "../platform/tegra/camera/camera_gpio.h"
#include "ar0820_mode_tbls.h"

/* ar0820 - sensor parameter limits */
#define AR0820_MIN_FRAME_LENGTH			0x0100
#define AR0820_MAX_FRAME_LENGTH			0xffff

/* ar0820 sensor register address */
#define AR0820_ANALOG_GAIN  0x3366
#define AR0820_ANALOG_GAIN2 0x336A

#define AR0820_FRAME_LENGTH_ADDR_MSB		0x0160
#define AR0820_FRAME_LENGTH_ADDR_LSB		0x0161

#define AR0820_COARSE_INTEG_TIME_ADDR	0X3012

static const struct of_device_id ar0820_of_match[] = {
	{ .compatible = "nvidia,ar0820", },
	{ },
};
MODULE_DEVICE_TABLE(of, ar0820_of_match);

static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

struct ar0820 {
	struct i2c_client		*i2c_client;
	struct v4l2_subdev		*subdev;
	struct device		*ser_dev;
	struct device		*dser_dev;
	struct gmsl_link_ctx	g_ctx;
	u16				fine_integ_time;
	u32				frame_length;
	struct camera_common_data	*s_data;
	struct tegracam_device		*tc_dev;
};

static bool volatile_reg(struct device *dev, unsigned int reg) {
	return true;
}


static const struct regmap_config sensor_regmap_config = {
	.reg_bits = 16,
	.val_bits = 16,
	.cache_type = REGCACHE_RBTREE,
	.use_single_rw = true,
	.volatile_reg = volatile_reg
};

static inline void ar0820_get_frame_length_regs(ar0820_reg *regs,
	u32 frame_length)
{
	regs->addr = AR0820_FRAME_LENGTH_ADDR_MSB;
	regs->val = (frame_length >> 8) & 0xff;
	(regs + 1)->addr = AR0820_FRAME_LENGTH_ADDR_LSB;
	(regs + 1)->val = (frame_length) & 0xff;
}

static inline void ar0820_get_coarse_integ_time_regs(ar0820_reg *regs,
	u16 coarse_time)
{
	regs->addr = AR0820_COARSE_INTEG_TIME_ADDR;
	regs->val = coarse_time & 0xffff;
}

static inline void ar0820_get_gain_reg(ar0820_reg *regs, u16 cgain, u16 fgain)
{
	regs->addr = AR0820_ANALOG_GAIN;
	regs->val = (cgain) & 0xffff;

	(regs + 1)->addr = AR0820_ANALOG_GAIN2;
	(regs + 1)->val = (fgain) & 0xffff;
}

static inline int ar0820_read_reg(struct camera_common_data *s_data,
	u16 addr, u16 *val)
{
	int err = 0;
	u32 reg_val = 0;

	err = regmap_read(s_data->regmap, addr, &reg_val);
	*val = reg_val & 0xffff;

	return err;
}

static inline int ar0820_write_reg(struct camera_common_data *s_data,
	u16 addr, u16 val)
{
	int err = 0;

	err = regmap_write(s_data->regmap, addr, val);
	if (err)
		dev_err(s_data->dev, "%s: i2c write failed, 0x%x = %x",
			__func__, addr, val);

	return err;
}

static inline int ar0820_read_reg_u8(struct camera_common_data *s_data,
	u16 addr, u8 *val)
{
	u16 reg_val;
	int err;

	err = regmap_read(s_data->regmap, addr, (uint *) &reg_val);
	*val = reg_val & 0xFF;

	dev_err(s_data->dev, "%s: i2c read u8, 0x%x = %x",
			__func__, addr, *val);

	return err;

}

static int ar0820_write_reg_u8(struct camera_common_data *s_data,
				u16 addr, u8 val)
{
	int err;
	struct device *dev = s_data->dev;

	err = regmap_write(s_data->regmap, addr, val);
	if (err)
		dev_err(dev, "%s:i2c write failed, 0x%x = %x\n",
			__func__, addr, val);


	dev_err(s_data->dev, "%s: i2c write u8, 0x%x = %x",
			__func__, addr, val);

	return err;
}

static int ar0820_write_table(struct ar0820 *priv, const ar0820_reg table[])
{
	struct tegracam_device *tc_dev = priv->tc_dev;
	struct device *dev = tc_dev->dev;
	int i = 0;
	int ret = 0;
	int retry;

	while (table[i].addr != AR0820_TABLE_END)
	{
		retry = 5;

		if(table[i].addr == AR0820_TABLE_WAIT_MS)
		{
			dev_dbg(dev, "%s: sleep %d\n", __func__, table[i].val);
			msleep(table[i].val);
			i++;
			continue;
		}

retry_sensor:
		ret = ar0820_write_reg(priv->s_data, table[i].addr, table[i].val);
		if (ret)
		{
			retry--;
			if (retry > 0) {
				dev_warn(dev, "ar0820_write_reg: try %d\n", retry);
				msleep(4);
				goto retry_sensor;
			}
			return -1;
		}
		/* ret = ar0820_read_reg(priv->s_data, table[i].addr, &val); */
		/* dev_dbg(dev, "table[i].addr = 0x%x, val = 0x%x\n", table[i].addr, val); */
		i++;
	}

	return 0;
}

static struct mutex serdes_lock__;

extern int max9295_write_reg(struct device *dev, u16 addr, u8 val);


static int ar0820_gmsl_serdes_setup(struct ar0820 *priv)
{
	int err = 0;
	int des_err = 0;
	struct device *dev;
/*	const unsigned short GPIO_REG_A = 0x2be;*/


	if (!priv || !priv->ser_dev || !priv->dser_dev || !priv->i2c_client)
		return -EINVAL;

	dev = &priv->i2c_client->dev;

	mutex_lock(&serdes_lock__);

	/* For now no separate power on required for serializer device */
	max9296_power_on(priv->dser_dev);

	/* setup serdes addressing and control pipeline */
	err = max9296_setup_link(priv->dser_dev, &priv->i2c_client->dev);
	if (err) {
		dev_err(dev, "gmsl deserializer link config failed\n");
		goto error;
	}

	err = max9295_setup_control(priv->ser_dev);
	/* proceed even if ser setup failed, to setup deser correctly */
	if (err)
		dev_err(dev, "gmsl serializer setup failed\n");

#if 0
	/* Write GPIO_REG_A to strobe reset on sensor */
	err = max9295_write_reg(priv->ser_dev, GPIO_REG_A, 0x80);
	if (err) {
		dev_err(dev, "failed to write ar0820 gpio reset on\n");
		goto error;
	}
	usleep_range(10, 20);

	err = max9295_write_reg(priv->ser_dev, GPIO_REG_A, 0x90);
	if (err) {
		dev_err(dev, "failed to write ar0820 gpio reset off\n");
		goto error;
	}	
	usleep_range(10, 20);
#endif

	des_err = max9296_setup_control(priv->dser_dev, &priv->i2c_client->dev);
	if (des_err) {
		dev_err(dev, "gmsl deserializer setup failed\n");
		/* overwrite err only if deser setup also failed */
		err = des_err;
	}

error:
	mutex_unlock(&serdes_lock__);
	return err;
}

static void ar0820_gmsl_serdes_reset(struct ar0820 *priv)
{
	mutex_lock(&serdes_lock__);

	/* reset serdes addressing and control pipeline */
	max9295_reset_control(priv->ser_dev);
	max9296_reset_control(priv->dser_dev, &priv->i2c_client->dev);

	max9296_power_off(priv->dser_dev);

	mutex_unlock(&serdes_lock__);
}


static int ar0820_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	/* ar0820 does not support group hold */
	return 0;
}

static int ar0820_set_gain(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = tc_dev->dev;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];
	ar0820_reg reg_list[2];
	u16 coarse_gain, fine_gain;
	u8 tgain = 0, i, err;

	/* translate value */
	tgain = (u8)(val / mode->control_properties.gain_factor - 1) & 0x03;
	coarse_gain = (tgain << 12) | (tgain << 8) | (tgain << 4) | (tgain);

	tgain = (u8)((val % mode->control_properties.gain_factor) / 7) & 0x0f;
	fine_gain = (tgain << 12) | (tgain << 8) | (tgain << 4) | (tgain);

	dev_dbg(dev, "%s: val %lld, coarse_gain 0x%x, fine_gain 0x%x\n",
			__func__, val, coarse_gain, fine_gain);

	ar0820_get_gain_reg(reg_list, coarse_gain, fine_gain);
	for (i=0; i<(sizeof(reg_list)/sizeof(reg_list[0])); i++) {
		err = ar0820_write_reg(s_data, reg_list[i].addr,
			 reg_list[i].val);
		if (err)
			goto fail;
	}

	return 0;

fail:
	dev_dbg(dev, "%s: GAIN control error\n", __func__);
	return err;
}

static int ar0820_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct ar0820 *priv = (struct ar0820 *)tc_dev->priv;
	struct device *dev = tc_dev->dev;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];

	int err = 0;
	ar0820_reg fl_regs[2];
	u32 frame_length;
	int i;
return 0;
	frame_length = (u32)(mode->signal_properties.pixel_clock.val *
		(u64)mode->control_properties.framerate_factor /
		mode->image_properties.line_length / val);

	if (frame_length < AR0820_MIN_FRAME_LENGTH)
		frame_length = AR0820_MIN_FRAME_LENGTH;
	else if (frame_length > AR0820_MAX_FRAME_LENGTH)
		frame_length = AR0820_MAX_FRAME_LENGTH;

	dev_dbg(dev,
		"%s: val: %llde-6 [fps], frame_length: %u [lines]\n",
		__func__, val, frame_length);

	ar0820_get_frame_length_regs(fl_regs, frame_length);
	for (i = 0; i < 2; i++) {
		err = ar0820_write_reg(s_data, fl_regs[i].addr, fl_regs[i].val);
		if (err) {
			dev_dbg(dev,
				"%s: frame_length control error\n", __func__);
			return err;
		}
	}

	priv->frame_length = frame_length;

	return 0;
}

static int ar0820_set_exposure(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = tc_dev->dev;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];
	ar0820_reg ct_regs[1];
	u16 coarse_time;
	int i, err = 0;

	coarse_time = val * mode->signal_properties.pixel_clock.val
		/ mode->control_properties.exposure_factor
		/ mode->image_properties.line_length;

	dev_dbg(dev, "%s: val: %lld [us], coarse_time: %d [lines]\n",
		__func__, val, coarse_time);

	ar0820_get_coarse_integ_time_regs(ct_regs, coarse_time);

	for (i = 0; i < 1; i++) {
		err = ar0820_write_reg(s_data, ct_regs[i].addr, ct_regs[i].val);
		if (err) {
			dev_dbg(dev,
				"%s: coarse_time control error\n", __func__);
			return err;
		}
	}

	return 0;
}

static struct tegracam_ctrl_ops ar0820_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.set_gain = ar0820_set_gain,
	.set_exposure = ar0820_set_exposure,
	.set_frame_rate = ar0820_set_frame_rate,
	.set_group_hold = ar0820_set_group_hold,
};

static int ar0820_power_on(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;

	dev_dbg(dev, "%s: power on\n", __func__);
	if (pdata && pdata->power_on) {
		err = pdata->power_on(pw);
		if (err)
			dev_err(dev, "%s failed.\n", __func__);
		else
			pw->state = SWITCH_ON;
		return err;
	}

	if (pw->reset_gpio) {
		if (gpio_cansleep(pw->reset_gpio))
			gpio_set_value_cansleep(pw->reset_gpio, 0);
		else
			gpio_set_value(pw->reset_gpio, 0);
	}

	if (unlikely(!(pw->avdd || pw->iovdd || pw->dvdd)))
		goto skip_power_seqn;

	usleep_range(10, 20);

	if (pw->avdd) {
		err = regulator_enable(pw->avdd);
		if (err)
			goto ar0820_avdd_fail;
	}

	if (pw->iovdd) {
		err = regulator_enable(pw->iovdd);
		if (err)
			goto ar0820_iovdd_fail;
	}

	if (pw->dvdd) {
		err = regulator_enable(pw->dvdd);
		if (err)
			goto ar0820_dvdd_fail;
	}

	usleep_range(10, 20);

skip_power_seqn:
	if (pw->reset_gpio) {
		if (gpio_cansleep(pw->reset_gpio))
			gpio_set_value_cansleep(pw->reset_gpio, 1);
		else
			gpio_set_value(pw->reset_gpio, 1);
	}

	/* Need to wait for t4 + t5 + t9 time as per the data sheet */
	/* t4 - 200us, t5 - 21.2ms, t9 - 1.2ms */
	usleep_range(23000, 23100);

	pw->state = SWITCH_ON;

	return 0;

ar0820_dvdd_fail:
	regulator_disable(pw->iovdd);

ar0820_iovdd_fail:
	regulator_disable(pw->avdd);

ar0820_avdd_fail:
	dev_err(dev, "%s failed.\n", __func__);

	return -ENODEV;
}

static int ar0820_power_off(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;

	dev_dbg(dev, "%s: power off\n", __func__);

	if (pdata && pdata->power_off) {
		err = pdata->power_off(pw);
		if (err) {
			dev_err(dev, "%s failed.\n", __func__);
			return err;
		}
	} else {
		if (pw->reset_gpio) {
			if (gpio_cansleep(pw->reset_gpio))
				gpio_set_value_cansleep(pw->reset_gpio, 0);
			else
				gpio_set_value(pw->reset_gpio, 0);
		}

		usleep_range(10, 10);

		if (pw->dvdd)
			regulator_disable(pw->dvdd);
		if (pw->iovdd)
			regulator_disable(pw->iovdd);
		if (pw->avdd)
			regulator_disable(pw->avdd);
	}

	pw->state = SWITCH_OFF;

	return 0;
}

static int ar0820_power_put(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;

	if (unlikely(!pw))
		return -EFAULT;

	if (likely(pw->dvdd))
		devm_regulator_put(pw->dvdd);

	if (likely(pw->avdd))
		devm_regulator_put(pw->avdd);

	if (likely(pw->iovdd))
		devm_regulator_put(pw->iovdd);

	pw->dvdd = NULL;
	pw->avdd = NULL;
	pw->iovdd = NULL;

	if (likely(pw->reset_gpio))
		gpio_free(pw->reset_gpio);

	return 0;
}

static int ar0820_power_get(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct clk *parent;
	int err = 0;

	if (!pdata) {
		dev_err(dev, "pdata missing\n");
		return -EFAULT;
	}

	/* Sensor MCLK (aka. INCK) */
	if (pdata->mclk_name) {
		pw->mclk = devm_clk_get(dev, pdata->mclk_name);
		if (IS_ERR(pw->mclk)) {
			dev_err(dev, "unable to get clock %s\n",
				pdata->mclk_name);
			return PTR_ERR(pw->mclk);
		}

		if (pdata->parentclk_name) {
			parent = devm_clk_get(dev, pdata->parentclk_name);
			if (IS_ERR(parent)) {
				dev_err(dev, "unable to get parent clock %s",
					pdata->parentclk_name);
			} else
				clk_set_parent(pw->mclk, parent);
		}
	}

	/* analog 2.8v */
	if (pdata->regulators.avdd)
		err |= camera_common_regulator_get(dev,
				&pw->avdd, pdata->regulators.avdd);
	/* IO 1.8v */
	if (pdata->regulators.iovdd)
		err |= camera_common_regulator_get(dev,
				&pw->iovdd, pdata->regulators.iovdd);
	/* dig 1.2v */
	if (pdata->regulators.dvdd)
		err |= camera_common_regulator_get(dev,
				&pw->dvdd, pdata->regulators.dvdd);
	if (err) {
		dev_err(dev, "%s: unable to get regulator(s)\n", __func__);
		goto done;
	}

	/* Reset or ENABLE GPIO */
	pw->reset_gpio = pdata->reset_gpio;
	if(pw->reset_gpio)
	{
		err = gpio_request(pw->reset_gpio, "cam_reset_gpio");
		if (err < 0) {
			dev_err(dev, "%s: unable to request reset_gpio (%d)\n",
				__func__, err);
			goto done;
		}
	}

done:
	pw->state = SWITCH_OFF;

	return err;
}

static struct camera_common_pdata *ar0820_parse_dt(
	struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct device_node *np = dev->of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;
	struct camera_common_pdata *ret = NULL;
	int err = 0;
	int gpio;

	if (!np)
		return NULL;

	match = of_match_device(ar0820_of_match, dev);
	if (!match) {
		dev_err(dev, "Failed to find matching dt id\n");
		return NULL;
	}

	board_priv_pdata = devm_kzalloc(dev,
		sizeof(*board_priv_pdata), GFP_KERNEL);
	if (!board_priv_pdata)
		return NULL;

	gpio = of_get_named_gpio(np, "reset-gpios", 0);
	if (gpio < 0) {
		if (gpio == -EPROBE_DEFER)
			ret = ERR_PTR(-EPROBE_DEFER);
		dev_err(dev, "reset-gpios not found\n");
		gpio = 0;
	}
	board_priv_pdata->reset_gpio = (unsigned int)gpio;

	err = of_property_read_string(np, "mclk", &board_priv_pdata->mclk_name);
	if (err)
		dev_dbg(dev, "mclk name not present, "
			"assume sensor driven externally\n");

	err = of_property_read_string(np, "avdd-reg",
		&board_priv_pdata->regulators.avdd);
	err |= of_property_read_string(np, "iovdd-reg",
		&board_priv_pdata->regulators.iovdd);
	err |= of_property_read_string(np, "dvdd-reg",
		&board_priv_pdata->regulators.dvdd);
	if (err)
		dev_dbg(dev, "avdd, iovdd and/or dvdd reglrs. not present, "
			"assume sensor powered independently\n");

	board_priv_pdata->has_eeprom =
		of_property_read_bool(np, "has-eeprom");

	return board_priv_pdata;

	devm_kfree(dev, board_priv_pdata);

	return ret;
}

static int ar0820_set_mode(struct tegracam_device *tc_dev)
{
	struct ar0820 *priv = (struct ar0820 *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = tc_dev->dev;
	int err = 0;

	dev_dbg(dev, "%s: s_data->mode %d\n", __func__, s_data->mode);

	err = ar0820_write_table(priv, mode_table[s_data->mode]);
	if (err)
		return err;

	return 0;
}

static int ar0820_start_streaming(struct tegracam_device *tc_dev)
{
	struct ar0820 *priv = (struct ar0820 *)tegracam_get_privdata(tc_dev);
	struct device *dev = tc_dev->dev;
	int err;

	/* enable serdes streaming */
	err = max9295_setup_streaming(priv->ser_dev);
	if (err)
		goto exit;
	err = max9296_setup_streaming(priv->dser_dev, dev);
	if (err)
		goto exit;
	err = max9296_start_streaming(priv->dser_dev, dev);
	if (err)
		goto exit;



	err = ar0820_write_table(priv, mode_table[AR0820_START_STREAM]);
	if (err)
		return err;

	dev_dbg(dev, "%s starting streaming\n", __func__);

	msleep(20);

	return 0;

exit:
	dev_err(dev, "%s: error setting stream\n", __func__);

	return err;

}

static int ar0820_stop_streaming(struct tegracam_device *tc_dev)
{
	int err;
	struct device *dev = tc_dev->dev;
	struct ar0820 *priv = (struct ar0820 *)tegracam_get_privdata(tc_dev);

	/* disable serdes streaming */
	max9296_stop_streaming(priv->dser_dev, dev);

	err = ar0820_write_table(priv, mode_table[AR0820_STOP_STREAM]);

	usleep_range(50000, 51000);

	return err;
}

static struct camera_common_sensor_ops ar0820_common_ops = {
	.numfrmfmts = ARRAY_SIZE(ar0820_frmfmt),
	.frmfmt_table = ar0820_frmfmt,
	.power_on = ar0820_power_on,
	.power_off = ar0820_power_off,
	.write_reg = ar0820_write_reg_u8,
	.read_reg = ar0820_read_reg_u8,
	.parse_dt = ar0820_parse_dt,
	.power_get = ar0820_power_get,
	.power_put = ar0820_power_put,
	.set_mode = ar0820_set_mode,
	.start_streaming = ar0820_start_streaming,
	.stop_streaming = ar0820_stop_streaming,
};

#if 0
static int ar0820_board_setup(struct ar0820 *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	int err = 0;

	if (pdata->mclk_name) {
		err = camera_common_mclk_enable(s_data);
		if (err) {
			dev_err(dev, "error turning on mclk (%d)\n", err);
			goto done;
		}
	}

	err = ar0820_power_on(s_data);
	if (err) {
		dev_err(dev, "error during power on sensor (%d)\n", err);
		goto err_power_on;
	}


	ar0820_power_off(s_data);

err_power_on:
	if (pdata->mclk_name)
		camera_common_mclk_disable(s_data);

done:
	return err;
}
#endif

static int ar0820_board_setup(struct ar0820 *priv)
{
	struct tegracam_device *tc_dev = priv->tc_dev;
	struct device *dev = tc_dev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *ser_node;
	struct i2c_client *ser_i2c = NULL;
	struct device_node *dser_node;
	struct i2c_client *dser_i2c = NULL;
	struct device_node *gmsl;
	int value = 0xFFFF;
	const char *str_value;
	const char *str_value1[2];
	int  i;
	int err;

	err = of_property_read_u32(node, "reg", &priv->g_ctx.sdev_reg);
	if (err < 0) {
		dev_err(dev, "reg not found\n");
		goto error;
	}

	err = of_property_read_u32(node, "def-addr",
					&priv->g_ctx.sdev_def);
	if (err < 0) {
		dev_err(dev, "def-addr not found\n");
		goto error;
	}

	ser_node = of_parse_phandle(node, "nvidia,gmsl-ser-device", 0);
	if (ser_node == NULL) {
		dev_err(dev,
			"missing %s handle\n",
				"nvidia,gmsl-ser-device");
		goto error;
	}

	err = of_property_read_u32(ser_node, "reg", &priv->g_ctx.ser_reg);
	if (err < 0) {
		dev_err(dev, "serializer reg not found\n");
		goto error;
	}

	ser_i2c = of_find_i2c_device_by_node(ser_node);
	of_node_put(ser_node);

	if (ser_i2c == NULL) {
		dev_err(dev, "missing serializer dev handle\n");
		goto error;
	}
	if (ser_i2c->dev.driver == NULL) {
		dev_err(dev, "missing seriailzer addr %d\n", ser_i2c->addr);
		dev_err(dev, "missing serializer driver - ask for reprobe?\n");
		//return -EPROBE_DEFER;
		goto error;
	}

	priv->ser_dev = &ser_i2c->dev;

	dser_node = of_parse_phandle(node, "nvidia,gmsl-dser-device", 0);
	if (dser_node == NULL) {
		dev_err(dev,
			"missing %s handle\n",
				"nvidia,gmsl-dser-device");
		goto error;
	}

	dser_i2c = of_find_i2c_device_by_node(dser_node);
	of_node_put(dser_node);

	if (dser_i2c == NULL) {
		dev_err(dev, "missing deserializer dev handle\n");
		goto error;
	}
	if (dser_i2c->dev.driver == NULL) {
		dev_err(dev, "missing deserializer driver - ask for reprobe?\n");
		//return -EPROBE_DEFER;
		goto error;
	}

	priv->dser_dev = &dser_i2c->dev;

	/* populate g_ctx from DT */
	gmsl = of_get_child_by_name(node, "gmsl-link");
	if (gmsl == NULL) {
		dev_err(dev, "missing gmsl-link device node\n");
		err = -EINVAL;
		goto error;
	}

	err = of_property_read_string(gmsl, "dst-csi-port", &str_value);
	if (err < 0) {
		dev_err(dev, "No dst-csi-port found\n");
		goto error;
	}
	priv->g_ctx.dst_csi_port =
		(!strcmp(str_value, "a")) ? GMSL_CSI_PORT_A : GMSL_CSI_PORT_B;

	err = of_property_read_string(gmsl, "src-csi-port", &str_value);
	if (err < 0) {
		dev_err(dev, "No src-csi-port found\n");
		goto error;
	}
	priv->g_ctx.src_csi_port =
		(!strcmp(str_value, "a")) ? GMSL_CSI_PORT_A : GMSL_CSI_PORT_B;

	err = of_property_read_string(gmsl, "csi-mode", &str_value);
	if (err < 0) {
		dev_err(dev, "No csi-mode found\n");
		goto error;
	}

	if (!strcmp(str_value, "1x4")) {
		priv->g_ctx.csi_mode = GMSL_CSI_1X4_MODE;
	} else if (!strcmp(str_value, "2x4")) {
		priv->g_ctx.csi_mode = GMSL_CSI_2X4_MODE;
	} else if (!strcmp(str_value, "4x2")) {
		priv->g_ctx.csi_mode = GMSL_CSI_4X2_MODE;
	} else if (!strcmp(str_value, "2x2")) {
		priv->g_ctx.csi_mode = GMSL_CSI_2X2_MODE;
	} else {
		dev_err(dev, "invalid csi mode\n");
		goto error;
	}

	err = of_property_read_string(gmsl, "serdes-csi-link", &str_value);
	if (err < 0) {
		dev_err(dev, "No serdes-csi-link found\n");
		goto error;
	}
	priv->g_ctx.serdes_csi_link =
		(!strcmp(str_value, "a")) ?
			GMSL_SERDES_CSI_LINK_A : GMSL_SERDES_CSI_LINK_B;

	err = of_property_read_u32(gmsl, "st-vc", &value);
	if (err < 0) {
		dev_err(dev, "No st-vc info\n");
		goto error;
	}
	priv->g_ctx.st_vc = value;

	err = of_property_read_u32(gmsl, "vc-id", &value);
	if (err < 0) {
		dev_err(dev, "No vc-id info\n");
		goto error;
	}
	priv->g_ctx.dst_vc = value;

	err = of_property_read_u32(gmsl, "num-lanes", &value);
	if (err < 0) {
		dev_err(dev, "No num-lanes info\n");
		goto error;
	}
	priv->g_ctx.num_csi_lanes = value;

	priv->g_ctx.num_streams =
			of_property_count_strings(gmsl, "streams");
	if (priv->g_ctx.num_streams <= 0) {
		dev_err(dev, "No streams found\n");
		err = -EINVAL;
		goto error;
	}

	for (i = 0; i < priv->g_ctx.num_streams; i++) {
		of_property_read_string_index(gmsl, "streams", i,
						&str_value1[i]);
		if (!str_value1[i]) {
			dev_err(dev, "invalid stream info\n");
			goto error;
		}
		if (!strcmp(str_value1[i], "raw12")) {
			priv->g_ctx.streams[i].st_data_type =
							GMSL_CSI_DT_RAW_12;
		} else if (!strcmp(str_value1[i], "embed")) {
			priv->g_ctx.streams[i].st_data_type =
							GMSL_CSI_DT_EMBED;
		} else if (!strcmp(str_value1[i], "ued-u1")) {
			priv->g_ctx.streams[i].st_data_type =
							GMSL_CSI_DT_UED_U1;
		} else {
			dev_err(dev, "invalid stream data type\n");
			goto error;
		}
	}

	priv->g_ctx.s_dev = dev;

	dev_dbg(dev, "board setup complete\n");

	return 0;

error:
	dev_err(dev, "board setup failed\n");
	return err;
}



static int ar0820_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);

	return 0;
}

static const struct v4l2_subdev_internal_ops ar0820_subdev_internal_ops = {
	.open = ar0820_open,
};

static int ar0820_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct tegracam_device *tc_dev;
	struct ar0820 *priv;
	int err;

	dev_dbg(dev, "probing v4l2 sensor at addr 0x%0x\n", client->addr);

	if (!IS_ENABLED(CONFIG_OF) || !client->dev.of_node)
		return -EINVAL;

	priv = devm_kzalloc(dev,
			sizeof(struct ar0820), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	tc_dev = devm_kzalloc(dev,
			sizeof(struct tegracam_device), GFP_KERNEL);
	if (!tc_dev)
		return -ENOMEM;


	priv->i2c_client = tc_dev->client = client;
	tc_dev->dev = dev;
	strncpy(tc_dev->name, "ar0820", sizeof(tc_dev->name));
	tc_dev->dev_regmap_config = &sensor_regmap_config;
	tc_dev->sensor_ops = &ar0820_common_ops;
	tc_dev->v4l2sd_internal_ops = &ar0820_subdev_internal_ops;
	tc_dev->tcctrl_ops = &ar0820_ctrl_ops;

	err = tegracam_device_register(tc_dev);
	if (err) {
		dev_err(dev, "tegra camera driver registration failed\n");
		return err;
	}
	priv->tc_dev = tc_dev;
	priv->s_data = tc_dev->s_data;
	priv->subdev = &tc_dev->s_data->subdev;
	tegracam_set_privdata(tc_dev, (void *)priv);

	err = ar0820_board_setup(priv);
	if (err) {
		tegracam_device_unregister(tc_dev);
		dev_err(dev, "board setup failed\n");
		return err;
	}

	/* Pair sensor to serializer dev */
	err = max9295_sdev_pair(priv->ser_dev, &priv->g_ctx);
	if (err) {
		tegracam_device_unregister(tc_dev);
		dev_err(&client->dev, "gmsl ser pairing failed\n");
		return err;
	}

	/* Register sensor to deserializer dev */
	err = max9296_sdev_register(priv->dser_dev, &priv->g_ctx);
	if (err) {
		tegracam_device_unregister(tc_dev);
		dev_err(&client->dev, "gmsl deserializer register failed\n");
		return err;
	}

	/*
	 * gmsl serdes setup
	 *
	 * Sensor power on/off should be the right place for serdes
	 * setup/reset. But the problem is, the total required delay
	 * in serdes setup/reset exceeds the frame wait timeout, looks to
	 * be related to multiple channel open and close sequence
	 * issue (#BUG 200477330).
	 * Once this bug is fixed, these may be moved to power on/off.
	 * The delays in serdes is as per guidelines and can't be reduced,
	 * so it is placed in probe/remove, though for that, deserializer
	 * would be powered on always post boot, until 1.2v is supplied
	 * to deserializer from CVB.
	 */
	err = ar0820_gmsl_serdes_setup(priv);
	if (err) {
		tegracam_device_unregister(tc_dev);
		dev_err(&client->dev,
			"%s gmsl serdes setup failed\n", __func__);
		return err;
	}

	/* Try to read the part ID */
	{
		unsigned int chip_id;
		err = regmap_read(priv->s_data->regmap, 0x3000, &chip_id);
		if (err) {
			tegracam_device_unregister(tc_dev);
			dev_err(dev, "unable to read sensor chip revision\n");
			return err;
		}

		dev_err(dev, "Read sensor chip ID %x\n", chip_id);
	}

	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err) {
		tegracam_device_unregister(tc_dev);
		dev_err(dev, "tegra camera subdev registration failed\n");
		return err;
	}

	dev_dbg(dev, "detected ar0820 sensor\n");

	return 0;
}

static int ar0820_remove(struct i2c_client *client)
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct ar0820 *priv = (struct ar0820 *)s_data->priv;

	ar0820_gmsl_serdes_reset(priv);


	tegracam_v4l2subdev_unregister(priv->tc_dev);
	tegracam_device_unregister(priv->tc_dev);

	return 0;
}

static const struct i2c_device_id ar0820_id[] = {
	{ "ar0820", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ar0820_id);

static struct i2c_driver ar0820_i2c_driver = {
	.driver = {
		.name = "ar0820",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ar0820_of_match),
	},
	.probe = ar0820_probe,
	.remove = ar0820_remove,
	.id_table = ar0820_id,
};
module_i2c_driver(ar0820_i2c_driver);

static int __init ar0820_init(void)
{
	mutex_init(&serdes_lock__);

	return i2c_add_driver(&ar0820_i2c_driver);
}

static void __exit ar0820_exit(void)
{
	mutex_destroy(&serdes_lock__);
}

module_init(ar0820_init);
module_exit(ar0820_exit);

MODULE_DESCRIPTION("Media Controller driver for Sony AR0820");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL v2");
