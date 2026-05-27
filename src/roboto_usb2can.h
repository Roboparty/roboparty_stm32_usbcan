#ifndef ROBOTO_USB2CAN_H_
#define ROBOTO_USB2CAN_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <cannectivity/usb/class/gs_usb.h>
#include <zephyr/logging/log.h>
#include "version.h"

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/msos_desc.h>
#include <zephyr/sys/byteorder.h>
#else
#include <zephyr/usb/usb_device.h>
#endif

/* MSOS 2.0 Descriptors for WinUSB Support */
#define COMPATIBLE_ID_WINUSB 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00
/* gs_usb DeviceInterfaceGUID (candleLight compatible) */
#define GS_USB_DEVICE_INTERFACE_GUID                                                               \
	'{', 0x00, 'c', 0x00, '6', 0x00, 'e', 0x00, '5', 0x00, '1', 0x00, '5', 0x00, 'a', 0x00,    \
		'2', 0x00, '-', 0x00, '8', 0x00, 'd', 0x00, 'c', 0x00, '6', 0x00, '-', 0x00, '4',  \
		0x00, 'f', 0x00, 'c', 0x00, '4', 0x00, '-', 0x00, 'a', 0x00, '0', 0x00, '3', 0x00, \
		'c', 0x00, '-', 0x00, '9', 0x00, '3', 0x00, '2', 0x00, '5', 0x00, '5', 0x00, '5',  \
		0x00, 'd', 0x00, '6', 0x00, '8', 0x00, 'e', 0x00, '6', 0x00, '}', 0x00, 0x00, 0x00

/* MSOS 2.0 Descriptor Structure */
struct msos2_descriptor {
	struct msosv2_descriptor_set_header header;
	struct msosv2_compatible_id compatible_id;
	struct msosv2_guids_property guids_property;
} __packed;

static const struct msos2_descriptor msos2_desc = {
	.header =
		{
			.wLength = sizeof(struct msosv2_descriptor_set_header),
			.wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR,
			.dwWindowsVersion = 0x06030000, /* Windows 8.1+ */
			.wTotalLength = sizeof(struct msos2_descriptor),
		},
	.compatible_id =
		{
			.wLength = sizeof(struct msosv2_compatible_id),
			.wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID,
			.CompatibleID = {COMPATIBLE_ID_WINUSB},
		},
	.guids_property =
		{
			.wLength = sizeof(struct msosv2_guids_property),
			.wDescriptorType = MS_OS_20_FEATURE_REG_PROPERTY,
			.wPropertyDataType = MS_OS_20_PROPERTY_DATA_REG_MULTI_SZ,
			.wPropertyNameLength = 42,
			.PropertyName = {DEVICE_INTERFACE_GUIDS_PROPERTY_NAME},
			.wPropertyDataLength = 80,
			.bPropertyData = {GS_USB_DEVICE_INTERFACE_GUID},
		},
};

/* BOS Descriptor: USB 2.0 Extension */
static const struct usb_bos_capability_lpm bos_cap_lpm = {
	.bLength = sizeof(struct usb_bos_capability_lpm),
	.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
	.bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,
	.bmAttributes = 0UL,
};

/* BOS Descriptor: Microsoft OS 2.0 Platform */
struct usb_bos_msosv2 {
	struct usb_bos_platform_descriptor platform;
	struct usb_bos_capability_msos cap;
} __packed;

static const struct usb_bos_msosv2 bos_cap_msosv2 = {
	.platform =
		{
			.bLength = sizeof(struct usb_bos_msosv2),
			.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
			.bDevCapabilityType = USB_BOS_CAPABILITY_PLATFORM,
			.bReserved = 0,
			.PlatformCapabilityUUID =
				{
					/* MS OS 2.0 Platform Capability ID */
					0xDF,
					0x60,
					0xDD,
					0xD8,
					0x89,
					0x45,
					0xC7,
					0x4C,
					0x9C,
					0xD2,
					0x65,
					0x9D,
					0x9E,
					0x64,
					0x8A,
					0x9F,
				},
		},
	.cap =
		{
			.dwWindowsVersion = sys_cpu_to_le32(0x06030000),
			.wMSOSDescriptorSetTotalLength = sys_cpu_to_le16(sizeof(msos2_desc)),
			.bMS_VendorCode = 0x01, /* Vendor Request Code */
			.bAltEnumCode = 0x00,
		},
};

/* CAN error frame monitoring configuration */
#define CAN_ERR_FRAME_THRESHOLD 50   /* Max 50 error frames allowed per second */
#define CAN_ERR_WINDOW_MS       1000 /* Statistics window 1 second */
#define CAN_ERR_PASSIVE_LIMIT   10   /* Force Bus-Off after 10 accumulated ERROR_PASSIVE */

/* CAN error monitor (per channel) */
struct can_error_monitor {
	uint32_t err_frame_count;   /* Error frame count in current window */
	uint32_t err_passive_count; /* Accumulated ERROR_PASSIVE count */
	int64_t window_start_ms;    /* Statistics window start time */
	bool throttled;             /* Whether throttling triggered */
	bool forced_busoff;         /* Whether Bus-Off forced */
};

static struct can_error_monitor err_monitors[1]
	__attribute__((unused)) = {0}; /* Single channel, extensible */
static const struct device *can_devices[]
	__attribute__((unused)) = {DEVICE_DT_GET(DT_NODELABEL(fdcan1))};

/* LED Status Enumeration */
enum led_status {
	/* USB Status (Blue LED) */
	LED_USB_READY, // USB ready for communication - medium blink
	LED_USB_ERROR, // USB error - fast blink
};

/* CAN Status (Yellow LED) */
enum can_led_status {
	CAN_LED_OFF,     // CAN off state (not started or bus off) - very slow blink
	CAN_LED_ACTIVE,  // CAN normal operation - medium blink
	CAN_LED_WARNING, // CAN warning - slow blink
	CAN_LED_ERROR,   // CAN error state (error passive or flood) - fast blink
};

/* LED status configuration */
#define MIN_STOP_INTERVAL_MS 1000 /* STOP events within 1 second are considered abnormal */
#define LED_TICK_MS          50   /* Timer triggers every 50ms */
#define LED_TICKS_ACTIVITY   2    /* Activity LED on for 2 ticks (100ms) */

/* LED blink patterns */
struct led_pattern {
	uint16_t on_ms;  // Duration on
	uint16_t off_ms; // Duration off
	int8_t repeat;   // Repeat count (-1=infinite loop)
};

/* LED blink patterns for USB status (Blue LED) */
static const struct led_pattern usb_led_patterns[] = {
	[LED_USB_READY] = {500, 500, -1}, // Medium blink (1Hz, ready state)
	[LED_USB_ERROR] = {100, 100, -1}, // Fast blink (error state)
};

/* LED blink patterns for CAN status (Yellow LED) */
static const struct led_pattern can_led_patterns[] = {
	[CAN_LED_OFF] = {50, 3950, -1},      // Very slow blink (every 4s, off state)
	[CAN_LED_ACTIVE] = {500, 500, -1},   // Medium blink (1Hz, normal operation)
	[CAN_LED_WARNING] = {200, 1800, -1}, // Slow blink (every 2s, warning state)
	[CAN_LED_ERROR] = {100, 100, -1},    // Fast blink (error state, same as USB error)
};

/**
 * @brief Initialize the status LED system
 *
 * Sets up GPIO pins and work queues for the three-LED status indication system.
 * Must be called before using any other LED functions.
 *
 * @return 0 on success, negative error code on failure
 */
int status_led_init(void);

/**
 * @brief Set USB LED status (Blue LED)
 *
 * Controls the blue LED to indicate USB system status with different blink patterns.
 *
 * @param status USB status to display (LED_USB_READY or LED_USB_ERROR)
 */
void status_led_usb_set(enum led_status status);

/**
 * @brief Set CAN LED status (Yellow LED)
 *
 * Controls the yellow LED to indicate CAN system status with different blink patterns.
 *
 * @param status CAN status to display (CAN_LED_OFF, CAN_LED_ACTIVE, etc.)
 */
void status_led_can_set(enum can_led_status status);

/**
 * @brief Indicate CAN bus activity (Green LED)
 *
 * Briefly flashes the green LED to show CAN data transmission or reception activity.
 * This function is typically called from interrupt context.
 */
void status_led_can_activity(void);

/**
 * @brief GS-USB event callback for LED status updates
 *
 * Callback function registered with the GS-USB stack to receive bus events
 * and update LED status accordingly.
 *
 * @param dev Pointer to the device
 * @param ch Channel number
 * @param event GS-USB event type
 * @param user_data User data pointer
 * @return 0 on success, negative error code on failure
 */
int status_led_event(const struct device *dev, uint16_t ch, enum gs_usb_event event,
		     void *user_data);

#endif /* ROBOTO_USB2CAN_H_ */
