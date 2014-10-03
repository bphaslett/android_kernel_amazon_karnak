/*
 * Panasonic MN88473 DVB-T/T2/C demodulator driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#include "mn88473_priv.h"

static struct dvb_frontend_ops mn88473_ops;

/* write multiple registers */
static int mn88473_wregs(struct mn88473_dev *dev, u16 reg, const u8 *val, int len)
{
#define MAX_WR_LEN 21
#define MAX_WR_XFER_LEN (MAX_WR_LEN + 1)
	int ret;
	u8 buf[MAX_WR_XFER_LEN];
	struct i2c_msg msg[1] = {
		{
			.addr = (reg >> 8) & 0xff,
			.flags = 0,
			.len = 1 + len,
			.buf = buf,
		}
	};

	if (WARN_ON(len > MAX_WR_LEN))
		return -EINVAL;

	buf[0] = (reg >> 0) & 0xff;
	memcpy(&buf[1], val, len);

	ret = i2c_transfer(dev->client[0]->adapter, msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		dev_warn(&dev->client[0]->dev,
				"i2c wr failed=%d reg=%02x len=%d\n",
				ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* read multiple registers */
static int mn88473_rregs(struct mn88473_dev *dev, u16 reg, u8 *val, int len)
{
#define MAX_RD_LEN 2
#define MAX_RD_XFER_LEN (MAX_RD_LEN)
	int ret;
	u8 buf[MAX_RD_XFER_LEN];
	struct i2c_msg msg[2] = {
		{
			.addr = (reg >> 8) & 0xff,
			.flags = 0,
			.len = 1,
			.buf = buf,
		}, {
			.addr = (reg >> 8) & 0xff,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf,
		}
	};

	if (WARN_ON(len > MAX_RD_LEN))
		return -EINVAL;

	buf[0] = (reg >> 0) & 0xff;

	ret = i2c_transfer(dev->client[0]->adapter, msg, 2);
	if (ret == 2) {
		memcpy(val, buf, len);
		ret = 0;
	} else {
		dev_warn(&dev->client[0]->dev,
				"i2c rd failed=%d reg=%02x len=%d\n",
				ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* write single register */
static int mn88473_wreg(struct mn88473_dev *dev, u16 reg, u8 val)
{
	return mn88473_wregs(dev, reg, &val, 1);
}

/* read single register */
static int mn88473_rreg(struct mn88473_dev *dev, u16 reg, u8 *val)
{
	return mn88473_rregs(dev, reg, val, 1);
}

static int mn88473_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 1000;
	return 0;
}

static int mn88473_set_frontend(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	u32 if_frequency;
	u8 delivery_system_val, if_val[3], bw_val[7];

	dev_dbg(&client->dev,
			"delivery_system=%u modulation=%u frequency=%u bandwidth_hz=%u symbol_rate=%u inversion=%d stream_id=%d\n",
			c->delivery_system, c->modulation,
			c->frequency, c->bandwidth_hz, c->symbol_rate,
			c->inversion, c->stream_id);

	if (!dev->warm) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
		delivery_system_val = 0x02;
		break;
	case SYS_DVBT2:
		delivery_system_val = 0x03;
		break;
	case SYS_DVBC_ANNEX_A:
		delivery_system_val = 0x04;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
	case SYS_DVBT2:
		if (c->bandwidth_hz <= 6000000) {
			/* IF 3570000 Hz, BW 6000000 Hz */
			memcpy(if_val, "\x24\x8e\x8a", 3);
			memcpy(bw_val, "\xe9\x55\x55\x1c\x29\x1c\x29", 7);
		} else if (c->bandwidth_hz <= 7000000) {
			/* IF 4570000 Hz, BW 7000000 Hz */
			memcpy(if_val, "\x2e\xcb\xfb", 3);
			memcpy(bw_val, "\xc8\x00\x00\x17\x0a\x17\x0a", 7);
		} else if (c->bandwidth_hz <= 8000000) {
			/* IF 4570000 Hz, BW 8000000 Hz */
			memcpy(if_val, "\x2e\xcb\xfb", 3);
			memcpy(bw_val, "\xaf\x00\x00\x11\xec\x11\xec", 7);
		} else {
			ret = -EINVAL;
			goto err;
		}
		break;
	case SYS_DVBC_ANNEX_A:
		/* IF 5070000 Hz, BW 8000000 Hz */
		memcpy(if_val, "\x33\xea\xb3", 3);
		memcpy(bw_val, "\xaf\x00\x00\x11\xec\x11\xec", 7);
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	/* program tuner */
	if (fe->ops.tuner_ops.set_params) {
		ret = fe->ops.tuner_ops.set_params(fe);
		if (ret)
			goto err;
	}

	if (fe->ops.tuner_ops.get_if_frequency) {
		ret = fe->ops.tuner_ops.get_if_frequency(fe, &if_frequency);
		if (ret)
			goto err;

		dev_dbg(&client->dev, "get_if_frequency=%d\n", if_frequency);
	} else {
		if_frequency = 0;
	}

	switch (if_frequency) {
	case 3570000:
	case 4570000:
	case 5070000:
		break;
	default:
		dev_err(&client->dev, "IF frequency %d not supported\n",
				if_frequency);
		ret = -EINVAL;
		goto err;
	}

	ret = mn88473_wregs(dev, 0x1c05, "\x00", 1);
	ret = mn88473_wregs(dev, 0x1cfb, "\x13", 1);
	ret = mn88473_wregs(dev, 0x1cef, "\x13", 1);
	ret = mn88473_wregs(dev, 0x1cf9, "\x13", 1);
	ret = mn88473_wregs(dev, 0x1c00, "\x18", 1);
	ret = mn88473_wregs(dev, 0x1c01, "\x01", 1);
	ret = mn88473_wregs(dev, 0x1c02, "\x21", 1);
	ret = mn88473_wreg(dev, 0x1c03, delivery_system_val);
	ret = mn88473_wregs(dev, 0x1c0b, "\x00", 1);

	for (i = 0; i < sizeof(if_val); i++) {
		ret = mn88473_wreg(dev, 0x1c10 + i, if_val[i]);
		if (ret)
			goto err;
	}

	for (i = 0; i < sizeof(bw_val); i++) {
		ret = mn88473_wreg(dev, 0x1c13 + i, bw_val[i]);
		if (ret)
			goto err;
	}

	ret = mn88473_wregs(dev, 0x1c2d, "\x3b", 1);
	ret = mn88473_wregs(dev, 0x1c2e, "\x00", 1);
	ret = mn88473_wregs(dev, 0x1c56, "\x0d", 1);
	ret = mn88473_wregs(dev, 0x1801, "\xba", 1);
	ret = mn88473_wregs(dev, 0x1802, "\x13", 1);
	ret = mn88473_wregs(dev, 0x1803, "\x80", 1);
	ret = mn88473_wregs(dev, 0x1804, "\xba", 1);
	ret = mn88473_wregs(dev, 0x1805, "\x91", 1);
	ret = mn88473_wregs(dev, 0x1807, "\xe7", 1);
	ret = mn88473_wregs(dev, 0x1808, "\x28", 1);
	ret = mn88473_wregs(dev, 0x180a, "\x1a", 1);
	ret = mn88473_wregs(dev, 0x1813, "\x1f", 1);
	ret = mn88473_wregs(dev, 0x1819, "\x03", 1);
	ret = mn88473_wregs(dev, 0x181d, "\xb0", 1);
	ret = mn88473_wregs(dev, 0x182a, "\x72", 1);
	ret = mn88473_wregs(dev, 0x182d, "\x00", 1);
	ret = mn88473_wregs(dev, 0x183c, "\x00", 1);
	ret = mn88473_wregs(dev, 0x183f, "\xf8", 1);
	ret = mn88473_wregs(dev, 0x1840, "\xf4", 1);
	ret = mn88473_wregs(dev, 0x1841, "\x08", 1);
	ret = mn88473_wregs(dev, 0x18d2, "\x29", 1);
	ret = mn88473_wregs(dev, 0x18d4, "\x55", 1);
	ret = mn88473_wregs(dev, 0x1a10, "\x10", 1);
	ret = mn88473_wregs(dev, 0x1a11, "\xab", 1);
	ret = mn88473_wregs(dev, 0x1a12, "\x0d", 1);
	ret = mn88473_wregs(dev, 0x1a13, "\xae", 1);
	ret = mn88473_wregs(dev, 0x1a14, "\x1d", 1);
	ret = mn88473_wregs(dev, 0x1a15, "\x9d", 1);
	ret = mn88473_wregs(dev, 0x1abe, "\x08", 1);
	ret = mn88473_wregs(dev, 0x1c09, "\x08", 1);
	ret = mn88473_wregs(dev, 0x1c08, "\x1d", 1);
	ret = mn88473_wregs(dev, 0x18b2, "\x37", 1);
	ret = mn88473_wregs(dev, 0x18d7, "\x04", 1);
	ret = mn88473_wregs(dev, 0x1c32, "\x80", 1);
	ret = mn88473_wregs(dev, 0x1c36, "\x00", 1);
	ret = mn88473_wregs(dev, 0x1cf8, "\x9f", 1);
	if (ret)
		goto err;

	dev->delivery_system = c->delivery_system;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	int ret;

	*status = 0;

	if (!dev->warm) {
		ret = -EAGAIN;
		goto err;
	}

	*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
			FE_HAS_SYNC | FE_HAS_LOCK;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_init(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	int ret, len, remaining;
	const struct firmware *fw = NULL;
	u8 *fw_file = MN88473_FIRMWARE;

	dev_dbg(&client->dev, "\n");

	if (dev->warm)
		return 0;

	/* request the firmware, this will block and timeout */
	ret = request_firmware(&fw, fw_file, &client->dev);
	if (ret) {
		dev_err(&client->dev, "firmare file '%s' not found\n", fw_file);
		goto err;
	}

	dev_info(&client->dev, "downloading firmware from file '%s'\n",
			fw_file);

	ret = mn88473_wreg(dev, 0x18f5, 0x03);
	if (ret)
		goto err;

	for (remaining = fw->size; remaining > 0;
			remaining -= (dev->i2c_wr_max - 1)) {
		len = remaining;
		if (len > (dev->i2c_wr_max - 1))
			len = (dev->i2c_wr_max - 1);

		ret = mn88473_wregs(dev, 0x18f6,
				&fw->data[fw->size - remaining], len);
		if (ret) {
			dev_err(&client->dev, "firmware download failed=%d\n",
					ret);
			goto err;
		}
	}

	ret = mn88473_wreg(dev, 0x18f5, 0x00);
	if (ret)
		goto err;

	release_firmware(fw);
	fw = NULL;

	/* warm state */
	dev->warm = true;

	return 0;
err:
	if (fw)
		release_firmware(fw);

	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_sleep(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	int ret;

	dev_dbg(&client->dev, "\n");

	ret = mn88473_wreg(dev, 0x1c05, 0x3e);
	if (ret)
		goto err;

	dev->delivery_system = SYS_UNDEFINED;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static struct dvb_frontend_ops mn88473_ops = {
	.delsys = {SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_AC},
	.info = {
		.name = "Panasonic MN88473",
		.caps =	FE_CAN_FEC_1_2                 |
			FE_CAN_FEC_2_3                 |
			FE_CAN_FEC_3_4                 |
			FE_CAN_FEC_5_6                 |
			FE_CAN_FEC_7_8                 |
			FE_CAN_FEC_AUTO                |
			FE_CAN_QPSK                    |
			FE_CAN_QAM_16                  |
			FE_CAN_QAM_32                  |
			FE_CAN_QAM_64                  |
			FE_CAN_QAM_128                 |
			FE_CAN_QAM_256                 |
			FE_CAN_QAM_AUTO                |
			FE_CAN_TRANSMISSION_MODE_AUTO  |
			FE_CAN_GUARD_INTERVAL_AUTO     |
			FE_CAN_HIERARCHY_AUTO          |
			FE_CAN_MUTE_TS                 |
			FE_CAN_2G_MODULATION           |
			FE_CAN_MULTISTREAM
	},

	.get_tune_settings = mn88473_get_tune_settings,

	.init = mn88473_init,
	.sleep = mn88473_sleep,

	.set_frontend = mn88473_set_frontend,

	.read_status = mn88473_read_status,
};

static int mn88473_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct mn88473_config *config = client->dev.platform_data;
	struct mn88473_dev *dev;
	int ret;
	u8 u8tmp;

	dev_dbg(&client->dev, "\n");

	/* Caller really need to provide pointer for frontend we create. */
	if (config->fe == NULL) {
		dev_err(&client->dev, "frontend pointer not defined\n");
		ret = -EINVAL;
		goto err;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	dev->client[0] = client;
	dev->i2c_wr_max = config->i2c_wr_max;

	/* check demod answers to I2C */
	ret = mn88473_rreg(dev, 0x1c00, &u8tmp);
	if (ret)
		goto err_kfree;

	/*
	 * Chip has three I2C addresses for different register pages. Used
	 * addresses are 0x18, 0x1a and 0x1c. We register two dummy clients,
	 * 0x1a and 0x1c, in order to get own I2C client for each register page.
	 */
	dev->client[1] = i2c_new_dummy(client->adapter, 0x1a);
	if (dev->client[1] == NULL) {
		ret = -ENODEV;
		dev_err(&client->dev, "I2C registration failed\n");
		if (ret)
			goto err_kfree;
	}
	i2c_set_clientdata(dev->client[1], dev);

	dev->client[2] = i2c_new_dummy(client->adapter, 0x1c);
	if (dev->client[2] == NULL) {
		ret = -ENODEV;
		dev_err(&client->dev, "2nd I2C registration failed\n");
		if (ret)
			goto err_client_1_i2c_unregister_device;
	}
	i2c_set_clientdata(dev->client[2], dev);

	/* create dvb_frontend */
	memcpy(&dev->fe.ops, &mn88473_ops, sizeof(struct dvb_frontend_ops));
	dev->fe.demodulator_priv = client;
	*config->fe = &dev->fe;
	i2c_set_clientdata(client, dev);

	dev_info(&dev->client[0]->dev, "Panasonic MN88473 successfully attached\n");
	return 0;

err_client_1_i2c_unregister_device:
	i2c_unregister_device(dev->client[1]);
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_remove(struct i2c_client *client)
{
	struct mn88473_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	i2c_unregister_device(dev->client[2]);
	i2c_unregister_device(dev->client[1]);

	kfree(dev);

	return 0;
}

static const struct i2c_device_id mn88473_id_table[] = {
	{"mn88473", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mn88473_id_table);

static struct i2c_driver mn88473_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "mn88473",
	},
	.probe		= mn88473_probe,
	.remove		= mn88473_remove,
	.id_table	= mn88473_id_table,
};

module_i2c_driver(mn88473_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Panasonic MN88473 DVB-T/T2/C demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(MN88473_FIRMWARE);
