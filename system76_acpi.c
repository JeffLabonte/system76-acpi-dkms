// SPDX-License-Identifier: GPL-2.0+
/*
 * System76 ACPI Driver
 *
 * Copyright (C) 2019 System76
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/pci_ids.h>
#include <linux/types.h>

struct system76_data {
	struct acpi_device *acpi_dev;
	struct led_classdev ap_led;
	struct led_classdev kb_led;
	enum led_brightness kb_brightness;
	enum led_brightness kb_toggle_brightness;
	int kb_color;
	struct device *therm;
	union acpi_object *nfan;
	union acpi_object *ntmp;
	struct input_dev *input;
};

static const struct acpi_device_id device_ids[] = {
	{"17761776", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, device_ids);

// Array of keyboard LED brightness levels
static const enum led_brightness kb_levels[] = {
	48,
	72,
	96,
	144,
	192,
	255
};

// Array of keyboard LED colors in 24-bit RGB format
static const int kb_colors[] = {
	0xFFFFFF,
	0x0000FF,
	0xFF0000,
	0xFF00FF,
	0x00FF00,
	0x00FFFF,
	0xFFFF00
};

// Get a System76 ACPI device value by name
static int system76_get(struct system76_data *data, char *method)
{
	acpi_handle handle;
	acpi_status status;
	unsigned long long ret = 0;

	handle = acpi_device_handle(data->acpi_dev);
	status = acpi_evaluate_integer(handle, method, NULL, &ret);
	if (ACPI_SUCCESS(status))
		return (int)ret;
	else
		return -1;
}

// Get a System76 ACPI device value by name with index
static int system76_get_index(struct system76_data *data, char *method, int index)
{
	union acpi_object obj;
	struct acpi_object_list obj_list;
	acpi_handle handle;
	acpi_status status;
	unsigned long long ret = 0;

	obj.type = ACPI_TYPE_INTEGER;
	obj.integer.value = index;
	obj_list.count = 1;
	obj_list.pointer = &obj;
	handle = acpi_device_handle(data->acpi_dev);
	status = acpi_evaluate_integer(handle, method, &obj_list, &ret);
	if (ACPI_SUCCESS(status))
		return (int)ret;
	else
		return -1;
}

// Get a System76 ACPI device object by name
static int system76_get_object(struct system76_data *data, char *method, union acpi_object **obj)
{
	acpi_handle handle;
	acpi_status status;
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };

	handle = acpi_device_handle(data->acpi_dev);
	status = acpi_evaluate_object(handle, method, NULL, &buf);
	if (ACPI_SUCCESS(status)) {
		*obj = (union acpi_object *)buf.pointer;
		return 0;
	} else {
		return -1;
	}
}

// Get a name from a System76 ACPI device object
static char * system76_name(union acpi_object *obj, int index) {
	if (obj && obj->type == ACPI_TYPE_PACKAGE && index <= obj->package.count) {
		if (obj->package.elements[index].type == ACPI_TYPE_STRING) {
			return obj->package.elements[index].string.pointer;
		}
	}
	return NULL;
}

// Set a System76 ACPI device value by name
static int system76_set(struct system76_data *data, char *method, int value)
{
	union acpi_object obj;
	struct acpi_object_list obj_list;
	acpi_handle handle;
	acpi_status status;

	obj.type = ACPI_TYPE_INTEGER;
	obj.integer.value = value;
	obj_list.count = 1;
	obj_list.pointer = &obj;
	handle = acpi_device_handle(data->acpi_dev);
	status = acpi_evaluate_object(handle, method, &obj_list, NULL);
	if (ACPI_SUCCESS(status))
		return 0;
	else
		return -1;
}

// Get the airplane mode LED brightness
static enum led_brightness ap_led_get(struct led_classdev *led)
{
	struct system76_data *data;
	int value;

	data = container_of(led, struct system76_data, ap_led);
	value = system76_get(data, "GAPL");
	if (value > 0)
		return (enum led_brightness)value;
	else
		return LED_OFF;
}

// Set the airplane mode LED brightness
static int ap_led_set(struct led_classdev *led, enum led_brightness value)
{
	struct system76_data *data;

	data = container_of(led, struct system76_data, ap_led);
	return system76_set(data, "SAPL", value == LED_OFF ? 0 : 1);
}

// Get the last set keyboard LED brightness
static enum led_brightness kb_led_get(struct led_classdev *led)
{
	struct system76_data *data;

	data = container_of(led, struct system76_data, kb_led);
	return data->kb_brightness;
}

// Set the keyboard LED brightness
static int kb_led_set(struct led_classdev *led, enum led_brightness value)
{
	struct system76_data *data;

	data = container_of(led, struct system76_data, kb_led);
	data->kb_brightness = value;
	return system76_set(data, "SKBL", (int)data->kb_brightness);
}

// Get the last set keyboard LED color
static ssize_t kb_led_color_show(
	struct device *dev,
	struct device_attribute *dev_attr,
	char *buf)
{
	struct led_classdev *led;
	struct system76_data *data;

	led = (struct led_classdev *)dev->driver_data;
	data = container_of(led, struct system76_data, kb_led);
	return sprintf(buf, "%06X\n", data->kb_color);
}

// Set the keyboard LED color
static ssize_t kb_led_color_store(
	struct device *dev,
	struct device_attribute *dev_attr,
	const char *buf,
	size_t size)
{
	struct led_classdev *led;
	struct system76_data *data;
	unsigned int val;
	int ret;

	led = (struct led_classdev *)dev->driver_data;
	data = container_of(led, struct system76_data, kb_led);
	ret = kstrtouint(buf, 16, &val);
	if (ret)
		return ret;
	if (val > 0xFFFFFF)
		return -EINVAL;
	data->kb_color = (int)val;
	system76_set(data, "SKBC", data->kb_color);

	return size;
}

static const struct device_attribute kb_led_color_dev_attr = {
	.attr = {
		.name = "color",
		.mode = 0644,
	},
	.show = kb_led_color_show,
	.store = kb_led_color_store,
};

// Notify that the keyboard LED was changed by hardware
static void kb_led_notify(struct system76_data *data)
{
	led_classdev_notify_brightness_hw_changed(
		&data->kb_led,
		data->kb_brightness
	);
}

// Read keyboard LED brightness as set by hardware
static void kb_led_hotkey_hardware(struct system76_data *data)
{
	int value;

	value = system76_get(data, "GKBL");
	if (value < 0)
		return;
	data->kb_brightness = value;
	kb_led_notify(data);
}

// Toggle the keyboard LED
static void kb_led_hotkey_toggle(struct system76_data *data)
{
	if (data->kb_brightness > 0) {
		data->kb_toggle_brightness = data->kb_brightness;
		kb_led_set(&data->kb_led, 0);
	} else {
		kb_led_set(&data->kb_led, data->kb_toggle_brightness);
	}
	kb_led_notify(data);
}

// Decrease the keyboard LED brightness
static void kb_led_hotkey_down(struct system76_data *data)
{
	int i;

	if (data->kb_brightness > 0) {
		for (i = ARRAY_SIZE(kb_levels); i > 0; i--) {
			if (kb_levels[i - 1] < data->kb_brightness) {
				kb_led_set(&data->kb_led, kb_levels[i - 1]);
				break;
			}
		}
	} else {
		kb_led_set(&data->kb_led, data->kb_toggle_brightness);
	}
	kb_led_notify(data);
}

// Increase the keyboard LED brightness
static void kb_led_hotkey_up(struct system76_data *data)
{
	int i;

	if (data->kb_brightness > 0) {
		for (i = 0; i < ARRAY_SIZE(kb_levels); i++) {
			if (kb_levels[i] > data->kb_brightness) {
				kb_led_set(&data->kb_led, kb_levels[i]);
				break;
			}
		}
	} else {
		kb_led_set(&data->kb_led, data->kb_toggle_brightness);
	}
	kb_led_notify(data);
}

// Cycle the keyboard LED color
static void kb_led_hotkey_color(struct system76_data *data)
{
	int i;

	if (data->kb_color < 0)
		return;
	if (data->kb_brightness > 0) {
		for (i = 0; i < ARRAY_SIZE(kb_colors); i++) {
			if (kb_colors[i] == data->kb_color)
				break;
		}
		i += 1;
		if (i >= ARRAY_SIZE(kb_colors))
			i = 0;
		data->kb_color = kb_colors[i];
		system76_set(data, "SKBC", data->kb_color);
	} else {
		kb_led_set(&data->kb_led, data->kb_toggle_brightness);
	}
	kb_led_notify(data);
}

static umode_t thermal_is_visible(const void *drvdata, enum hwmon_sensor_types type, u32 attr, int channel) {
	const struct system76_data *data = drvdata;

	if (type == hwmon_fan || type == hwmon_pwm) {
		if (system76_name(data->nfan, channel)) {
			return S_IRUGO;
		}
	} else if (type == hwmon_temp) {
		if (system76_name(data->ntmp, channel)) {
			return S_IRUGO;
		}
	}
	return 0;
}

static int thermal_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val) {
	struct system76_data *data = dev_get_drvdata(dev);
	int raw;

	if (type == hwmon_fan && attr == hwmon_fan_input) {
		raw = system76_get_index(data, "GFAN", channel);
		if (raw >= 0) {
			*val = (long)((raw >> 8) & 0xFFFF);
			return 0;
		}
	} else if (type == hwmon_pwm && attr == hwmon_pwm_input) {
		raw = system76_get_index(data, "GFAN", channel);
		if (raw >= 0) {
			*val = (long)(raw & 0xFF);
			return 0;
		}
	} else if (type == hwmon_temp && attr == hwmon_temp_input) {
		raw = system76_get_index(data, "GTMP", channel);
		if (raw >= 0) {
			*val = (long)(raw * 1000);
			return 0;
		}
	}
	return -EINVAL;
}

static int thermal_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, const char **str) {
	struct system76_data *data = dev_get_drvdata(dev);

	if (type == hwmon_fan && attr == hwmon_fan_label) {
		*str = system76_name(data->nfan, channel);
		if (*str)
			return 0;
	} else if (type == hwmon_temp && attr == hwmon_temp_label) {
		*str = system76_name(data->ntmp, channel);
		if (*str)
			return 0;
	}
	return -EINVAL;
}

static const struct hwmon_ops thermal_ops = {
	.is_visible = thermal_is_visible,
	.read = thermal_read,
	.read_string = thermal_read_string,
};

// Allocate up to 8 fans and temperatures
static const struct hwmon_channel_info *thermal_channel_info[] = {
	HWMON_CHANNEL_INFO(fan,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(pwm,
			HWMON_PWM_INPUT,
			HWMON_PWM_INPUT,
			HWMON_PWM_INPUT,
			HWMON_PWM_INPUT,
			HWMON_PWM_INPUT,
			HWMON_PWM_INPUT,
			HWMON_PWM_INPUT,
			HWMON_PWM_INPUT),
	HWMON_CHANNEL_INFO(temp,
			HWMON_T_INPUT | HWMON_T_LABEL,
			HWMON_T_INPUT | HWMON_T_LABEL,
			HWMON_T_INPUT | HWMON_T_LABEL,
			HWMON_T_INPUT | HWMON_T_LABEL,
			HWMON_T_INPUT | HWMON_T_LABEL,
			HWMON_T_INPUT | HWMON_T_LABEL,
			HWMON_T_INPUT | HWMON_T_LABEL,
			HWMON_T_INPUT | HWMON_T_LABEL),
	NULL
};

static const struct hwmon_chip_info thermal_chip_info = {
	.ops = &thermal_ops,
	.info = thermal_channel_info,
};

static void input_key(struct system76_data *data, unsigned int code) {
	input_report_key(data->input, code, 1);
	input_sync(data->input);

	input_report_key(data->input, code, 0);
	input_sync(data->input);
}

// Handle ACPI notification
static void system76_notify(struct acpi_device *acpi_dev, u32 event)
{
	struct system76_data *data;

	data = acpi_driver_data(acpi_dev);
	switch (event) {
	case 0x80:
		kb_led_hotkey_hardware(data);
		break;
	case 0x81:
		kb_led_hotkey_toggle(data);
		break;
	case 0x82:
		kb_led_hotkey_down(data);
		break;
	case 0x83:
		kb_led_hotkey_up(data);
		break;
	case 0x84:
		kb_led_hotkey_color(data);
		break;
	case 0x85:
		input_key(data, KEY_SCREENLOCK);
		break;
	}
}

// Add a System76 ACPI device
static int system76_add(struct acpi_device *acpi_dev)
{
	struct system76_data *data;
	int err;

	data = devm_kzalloc(&acpi_dev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	acpi_dev->driver_data = data;
	data->acpi_dev = acpi_dev;

	err = system76_get(data, "INIT");
	if (err)
		return err;
	data->ap_led.name = "system76_acpi::airplane";
	data->ap_led.flags = LED_CORE_SUSPENDRESUME;
	data->ap_led.brightness_get = ap_led_get;
	data->ap_led.brightness_set_blocking = ap_led_set;
	data->ap_led.max_brightness = 1;
	data->ap_led.default_trigger = "rfkill-none";
	err = devm_led_classdev_register(&acpi_dev->dev, &data->ap_led);
	if (err)
		return err;

	data->kb_led.name = "system76_acpi::kbd_backlight";
	data->kb_led.flags = LED_BRIGHT_HW_CHANGED | LED_CORE_SUSPENDRESUME;
	data->kb_led.brightness_get = kb_led_get;
	data->kb_led.brightness_set_blocking = kb_led_set;
	if (acpi_has_method(acpi_device_handle(data->acpi_dev), "SKBC")) {
		data->kb_led.max_brightness = 255;
		data->kb_toggle_brightness = 72;
		data->kb_color = 0xffffff;
		system76_set(data, "SKBC", data->kb_color);
	} else {
		data->kb_led.max_brightness = 5;
		data->kb_color = -1;
	}
	err = devm_led_classdev_register(&acpi_dev->dev, &data->kb_led);
	if (err)
		return err;

	if (data->kb_color >= 0) {
		err = device_create_file(
			data->kb_led.dev,
			&kb_led_color_dev_attr
		);
		if (err)
			return err;
	}

	system76_get_object(data, "NFAN", &data->nfan);
	system76_get_object(data, "NTMP", &data->ntmp);
	data->therm = devm_hwmon_device_register_with_info(&acpi_dev->dev, "system76_acpi", data, &thermal_chip_info, NULL);
	if (IS_ERR(data->therm))
		return PTR_ERR(data->therm);

	data->input = devm_input_allocate_device(&acpi_dev->dev);
	if (!data->input)
		return -ENOMEM;
	data->input->name = "System76 ACPI Hotkeys";
	data->input->phys = "system76_acpi/input0";
	data->input->id.bustype = BUS_HOST;
	data->input->dev.parent = &acpi_dev->dev;
	set_bit(EV_KEY, data->input->evbit);
	set_bit(KEY_SCREENLOCK, data->input->keybit);
	err = input_register_device(data->input);
	if (err) {
		input_free_device(data->input);
		return err;
	}

	return 0;
}

// Remove a System76 ACPI device
static int system76_remove(struct acpi_device *acpi_dev)
{
	struct system76_data *data;

	data = acpi_driver_data(acpi_dev);
	if (data->kb_color >= 0)
		device_remove_file(data->kb_led.dev, &kb_led_color_dev_attr);

	devm_led_classdev_unregister(&acpi_dev->dev, &data->ap_led);

	devm_led_classdev_unregister(&acpi_dev->dev, &data->kb_led);
	if (data->nfan)
		kfree(data->nfan);
	if (data->ntmp)
		kfree(data->ntmp);

	system76_get(data, "FINI");

	return 0;
}

static struct acpi_driver system76_driver = {
	.name = "System76 ACPI Driver",
	.class = "hotkey",
	.ids = device_ids,
	.ops = {
		.add = system76_add,
		.remove = system76_remove,
		.notify = system76_notify,
	},
};
module_acpi_driver(system76_driver);

MODULE_DESCRIPTION("System76 ACPI Driver");
MODULE_AUTHOR("Jeremy Soller <jeremy@system76.com>");
MODULE_LICENSE("GPL");
