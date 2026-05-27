/*
 * Status LED management for roboto_usb2can
 */

#include "roboto_usb2can.h"

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

/* LED hardware definitions */
const struct gpio_dt_spec usb_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
const struct gpio_dt_spec activity_led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
const struct gpio_dt_spec can_led = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

/* LED work queues */
struct k_work_delayable usb_led_work;
struct k_work_delayable can_led_work;
struct k_work_delayable activity_work;

/* LED state variables */
enum led_status current_usb_status = LED_USB_READY;
int usb_repeat_count = 0;
bool usb_led_state = false;
enum can_led_status current_can_status = CAN_LED_OFF;
int can_repeat_count = 0;
bool can_led_state = false;

/* LED activity timer and state */
struct k_timer activity_tick_timer;
k_timepoint_t last_activity_time;
int activity_ticks = 0;
k_timepoint_t last_stopped_time;

/* USB LED blink work (Blue LED) */
static void usb_led_blink_work(struct k_work *work)
{
	const struct led_pattern *pattern = &usb_led_patterns[current_usb_status];
	uint32_t next_delay;

	/* Toggle LED state */
	usb_led_state = !usb_led_state;
	gpio_pin_set_dt(&usb_led, usb_led_state);

	/* Calculate next trigger time */
	if (usb_led_state) {
		next_delay = pattern->on_ms;
	} else {
		next_delay = pattern->off_ms;

		/* Check repeat count */
		if (pattern->repeat > 0) {
			usb_repeat_count++;
			if (usb_repeat_count >= pattern->repeat) {
				/* Completed specified count, stop */
				gpio_pin_set_dt(&usb_led, 0);
				return;
			}
		}
	}

	/* Reschedule work */
	k_work_reschedule(&usb_led_work, K_MSEC(next_delay));
}

/* CAN LED blink work (Yellow LED) */
static void can_led_blink_work(struct k_work *work)
{
	const struct led_pattern *pattern = &can_led_patterns[current_can_status];
	uint32_t next_delay;

	/* Toggle LED state */
	can_led_state = !can_led_state;
	gpio_pin_set_dt(&can_led, can_led_state);

	/* Calculate next trigger time */
	if (can_led_state) {
		next_delay = pattern->on_ms;
	} else {
		next_delay = pattern->off_ms;

		/* Check repeat count */
		if (pattern->repeat > 0) {
			can_repeat_count++;
			if (can_repeat_count >= pattern->repeat) {
				/* Completed specified count, stop */
				gpio_pin_set_dt(&can_led, 0);
				return;
			}
		}
	}

	/* Reschedule work */
	k_work_reschedule(&can_led_work, K_MSEC(next_delay));
}

/* Activity LED turn off work */
static void activity_led_off_work(struct k_work *work)
{
	gpio_pin_set_dt(&activity_led, 0);
}

/* Activity LED timer callback (triggers every 50ms) */
static void activity_tick_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	if (activity_ticks > 0) {
		activity_ticks--;
		if (activity_ticks == (LED_TICKS_ACTIVITY / 2)) {
			/* Turn on after middle tick */
			gpio_pin_set_dt(&activity_led, 1);
		} else if (activity_ticks == 0) {
			/* Turn off at end */
			gpio_pin_set_dt(&activity_led, 0);
		}
	}
}

/* Initialize status LED */
int status_led_init(void)
{
	int ret;

	/* Initialize USB LED (Blue) */
	if (!gpio_is_ready_dt(&usb_led)) {
		LOG_ERR("USB LED GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&usb_led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure USB LED: %d", ret);
		return ret;
	}

	/* Initialize Activity LED (Green) */
	if (!gpio_is_ready_dt(&activity_led)) {
		LOG_WRN("Activity LED GPIO not ready");
	} else {
		ret = gpio_pin_configure_dt(&activity_led, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_WRN("Failed to configure activity LED: %d", ret);
		} else {
			k_work_init_delayable(&activity_work, activity_led_off_work);
			LOG_INF("Activity LED initialized");
		}
	}

	/* Initialize CAN LED (Yellow) */
	if (!gpio_is_ready_dt(&can_led)) {
		LOG_WRN("CAN LED GPIO not ready");
	} else {
		ret = gpio_pin_configure_dt(&can_led, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_WRN("Failed to configure CAN LED: %d", ret);
		} else {
			k_work_init_delayable(&can_led_work, can_led_blink_work);
			LOG_INF("CAN LED initialized");
		}
	}

	/* Initialize work queues */
	k_work_init_delayable(&usb_led_work, usb_led_blink_work);

	/* Initialize activity LED timer */
	k_timer_init(&activity_tick_timer, activity_tick_handler, NULL);
	k_timer_start(&activity_tick_timer, K_MSEC(LED_TICK_MS), K_MSEC(LED_TICK_MS));
	last_activity_time = sys_timepoint_calc(K_NO_WAIT);
	last_stopped_time = sys_timepoint_calc(K_NO_WAIT); /* Initialize STOPPED filter */

	/* Start with USB ready state */
	status_led_usb_set(LED_USB_READY);
	status_led_can_set(CAN_LED_OFF);

	LOG_INF("Status LEDs initialized");
	return 0;
}

/* Set USB LED status (Blue LED) */
void status_led_usb_set(enum led_status status)
{
	if (status >= ARRAY_SIZE(usb_led_patterns)) {
		LOG_ERR("Invalid USB LED status: %d", status);
		return;
	}

	/* Cancel current blink */
	k_work_cancel_delayable(&usb_led_work);

	current_usb_status = status;
	usb_repeat_count = 0;
	usb_led_state = false;

	/* Start new blink pattern */
	const struct led_pattern *pattern = &usb_led_patterns[status];
	gpio_pin_set_dt(&usb_led, 1);
	k_work_reschedule(&usb_led_work, K_MSEC(pattern->on_ms));

	LOG_DBG("USB LED status changed to %d", status);
}

/* Set CAN LED status (Yellow LED) */
void status_led_can_set(enum can_led_status status)
{
	if (status >= ARRAY_SIZE(can_led_patterns)) {
		LOG_ERR("Invalid CAN LED status: %d", status);
		return;
	}

	/* Cancel current blink */
	k_work_cancel_delayable(&can_led_work);

	current_can_status = status;
	can_repeat_count = 0;
	can_led_state = false;

	/* Start new blink pattern */
	const struct led_pattern *pattern = &can_led_patterns[status];
	gpio_pin_set_dt(&can_led, 1);
	k_work_reschedule(&can_led_work, K_MSEC(pattern->on_ms));

	LOG_DBG("CAN LED status changed to %d", status);
}

/* CAN activity indication (independently controls green LED, does not affect blue LED) */
void status_led_can_activity(void)
{
	if (!gpio_is_ready_dt(&activity_led)) {
		return;
	}

	/* Use timer counter method */
	activity_ticks = LED_TICKS_ACTIVITY;
}

/* gs_usb event callback function */
int status_led_event(const struct device *dev, uint16_t ch, enum gs_usb_event event,
		     void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(ch);
	ARG_UNUSED(user_data);

	switch (event) {
	case GS_USB_EVENT_CHANNEL_ACTIVITY_RX:
		__fallthrough;
	case GS_USB_EVENT_CHANNEL_ACTIVITY_TX:
		/* Low-pass filter: prevent LED from blinking too fast */
		if (!sys_timepoint_expired(last_activity_time)) {
			return 0; /* Ignore frequent events */
		}
		last_activity_time = sys_timepoint_calc(K_MSEC(LED_TICK_MS * LED_TICKS_ACTIVITY));
		status_led_can_activity();
		break;

	case GS_USB_EVENT_CHANNEL_STARTED:
		LOG_DBG("Channel %u started", ch);
		last_stopped_time = sys_timepoint_calc(K_NO_WAIT); /* Reset STOPPED filter */
		/* USB is ready, CAN channel started */
		status_led_usb_set(LED_USB_READY);
		status_led_can_set(CAN_LED_ACTIVE);
		break;

	case GS_USB_EVENT_CHANNEL_STOPPED:
		/* Filter spurious STOP events from Linux 5/6.1 */
		if (!sys_timepoint_expired(last_stopped_time)) {
			LOG_WRN("Channel %u: Ignoring spurious STOPPED event (possible Linux "
				"kernel bug)",
				ch);
			return 0; /* Ignore spurious events */
		}
		last_stopped_time = sys_timepoint_calc(K_MSEC(MIN_STOP_INTERVAL_MS));
		LOG_DBG("Channel %u stopped", ch);
		/* CAN stopped, USB still ready */
		status_led_usb_set(LED_USB_READY);
		status_led_can_set(CAN_LED_OFF);
		break;

	default:
		/* Ignore other events */
		break;
	}

	return 0;
}
