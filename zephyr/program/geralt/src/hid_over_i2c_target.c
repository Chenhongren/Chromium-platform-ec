#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <string.h>

#include "hid_i2c.h"
#include "usb_hid_touchpad.h"

struct i2c_target_dev_config {
	/* I2C alternate configuration */
	struct i2c_dt_spec bus;
};

struct i2c_target_data {
	struct i2c_target_config config;
	uint8_t write_buf[CONFIG_I2C_TARGET_IT8XXX2_MAX_BUF_SIZE];
	uint8_t read_buf[CONFIG_I2C_TARGET_IT8XXX2_MAX_BUF_SIZE];
	int write_buf_len;
};

extern struct k_msgq touchpad_report_queue;

const static struct device *one_wire_uart = DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

static int hid_handler(const uint8_t *in, int in_size, uint8_t *out)
{
	static int in_reset = true;

	memset(out, 0, CONFIG_I2C_TARGET_MAX_BUFFER_SIZE);

	if (in_size == 0) { /* read report */
		if (!in_reset) {
			int ret;

			ret = k_msgq_get(&touchpad_report_queue, out + 2, K_NO_WAIT);
			if (ret == 0) {
				*(uint16_t*)out = sizeof(struct usb_hid_touchpad_report);
			}

			if (k_msgq_num_used_get(&touchpad_report_queue) == 0) {
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_ap_hid_int_odl), 1);
			}

			return (ret ? 0 : sizeof(struct usb_hid_touchpad_report)) + 2;
		} else {
			/* first report after reset is always [0x00, 0x00] */
			in_reset = false;
			return 2;
		}
	}

	int reg = in[0];

	if (reg == 0x01) {
		memcpy(out, HID_DESC, sizeof(HID_DESC));
		return sizeof(HID_DESC);
	}

	if (reg == 0x02) {
		memcpy(out, REPORT_DESC, sizeof(REPORT_DESC));
		return sizeof(REPORT_DESC);
	}

	if (reg == 0x05) { /* reset */
		k_msgq_purge(&touchpad_report_queue);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_ap_hid_int_odl), 0);
		in_reset = true;
		return 0;
	}

	return 0;
}

static int hid_i2c_target_stop(struct i2c_target_config *config)
{
	struct i2c_target_data *data = CONTAINER_OF(config, struct i2c_target_data, config);

	if (data->write_buf_len) {
		hid_handler(data->write_buf, data->write_buf_len, data->read_buf);
	}
	data->write_buf_len = 0;
	return 0;
}

static void hid_i2c_target_buf_write_received(struct i2c_target_config *config,
					      uint8_t *ptr, uint32_t len)
{
	struct i2c_target_data *data = CONTAINER_OF(config, struct i2c_target_data, config);

	memcpy(data->write_buf, ptr, len);
	data->write_buf_len = len;
	return 0;
}

static int hid_i2c_target_buf_read_requested(struct i2c_target_config *config,
					     uint8_t **ptr, uint32_t *len)
{
	struct i2c_target_data *data = CONTAINER_OF(config, struct i2c_target_data, config);
	int out_len = hid_handler(data->write_buf, data->write_buf_len, data->read_buf);

	data->write_buf_len = 0;
	*ptr = data->read_buf;
	*len = out_len;

	return 0;
}

static const struct i2c_target_callbacks target_callbacks = {
	.buf_write_received = hid_i2c_target_buf_write_received,
	.buf_read_requested = hid_i2c_target_buf_read_requested,
	.stop = hid_i2c_target_stop,
};

static int hid_i2c_target_register(const struct device *dev)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	return i2c_target_register(cfg->bus.bus, &data->config);
}

static int hid_i2c_target_unregister(const struct device *dev)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	return i2c_target_unregister(cfg->bus.bus, &data->config);
}

static const struct i2c_target_driver_api api_funcs = {
	.driver_register = hid_i2c_target_register,
	.driver_unregister = hid_i2c_target_unregister,
};

static int hid_i2c_target_init(const struct device *dev)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	/* Check I2C controller ready. */
	if (!device_is_ready(cfg->bus.bus)) {
		return -ENODEV;
	}

	data->config.address = cfg->bus.addr;
	data->config.callbacks = &target_callbacks;

	return 0;
}

/* TODO: migrate to instance based api */

static const struct i2c_target_dev_config i2c_target_cfg = {
	.bus = I2C_DT_SPEC_GET(DT_NODELABEL(i2c5_target)),
};

static struct i2c_target_data i2c_target_data;

I2C_DEVICE_DT_DEFINE(DT_NODELABEL(i2c5_target), hid_i2c_target_init,
			  NULL,
			  &i2c_target_data,
			  &i2c_target_cfg,
			  POST_KERNEL,
			  CONFIG_I2C_TARGET_INIT_PRIORITY,
			  &api_funcs);
