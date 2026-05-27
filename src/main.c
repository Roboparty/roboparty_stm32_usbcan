#include "roboto_usb2can.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
/* New USB stack: Define USB device context */
USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1D50, 0x606F);

USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, "wentywenty");
USBD_DESC_PRODUCT_DEFINE(product, "roboto_usb2can");
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);
USBD_DESC_CONFIG_DEFINE(fs_config_desc, "Full-Speed Configuration");
USBD_CONFIGURATION_DEFINE(fs_config, 0, 250, &fs_config_desc);
#endif
/**
 * @brief Microsoft OS 2.0 descriptor vendor request handler
 *
 * Handles Windows-specific vendor requests for MSOS 2.0 descriptors,
 * enabling automatic WinUSB driver binding without manual driver installation.
 *
 * @param ctx USB device context
 * @param setup USB setup packet containing the request
 * @param buf Network buffer for response data
 * @return 0 on success, -ENOTSUP for unsupported requests
 */
static int msos_vendor_handler(const struct usbd_context *const ctx,
			       const struct usb_setup_packet *const setup,
			       struct net_buf *const buf)
{
	if (setup->bRequest == 0x01 && setup->wIndex == MS_OS_20_DESCRIPTOR_INDEX) {
		size_t len = sizeof(msos2_desc);
		net_buf_add_mem(buf, &msos2_desc, MIN(net_buf_tailroom(buf), len));
		LOG_INF("Windows requested MSOS2 descriptor");
		return 0;
	}
	return -ENOTSUP;
}

/* Register BOS Descriptors */
USBD_DESC_BOS_DEFINE(bos_lpm, sizeof(bos_cap_lpm), &bos_cap_lpm);
USBD_DESC_BOS_VREQ_DEFINE(bos_msosv2, sizeof(bos_cap_msosv2), &bos_cap_msosv2, 0x01,
			  msos_vendor_handler, NULL);

/**
 * @brief CAN state change callback - Error monitoring and protection
 *
 * This callback monitors CAN bus state changes and implements error protection
 * mechanisms including error frame flood detection and automatic bus-off recovery.
 *
 * @param dev Pointer to the CAN device
 * @param state Current CAN bus state
 * @param err_cnt CAN error counters (TX/RX)
 * @param user_data User data pointer (channel index)
 */
static void can_state_change_callback(const struct device *dev, enum can_state state,
				      struct can_bus_err_cnt err_cnt, void *user_data)
{
	int ch = 0; /* Currently only supports single channel, multi-channel needs matching via
		       user_data or dev */
	struct can_error_monitor *mon = &err_monitors[ch];
	int64_t now = k_uptime_get();

	/* Reset statistics window */
	if ((now - mon->window_start_ms) > CAN_ERR_WINDOW_MS) {
		mon->err_frame_count = 0;
		mon->window_start_ms = now;
		mon->throttled = false;
	}

	/* Error frame count */
	if (state != CAN_STATE_ERROR_ACTIVE) {
		mon->err_frame_count++;
	}

	/* Error frame flood detection */
	if (mon->err_frame_count > CAN_ERR_FRAME_THRESHOLD) {
		if (!mon->throttled) {
			LOG_ERR("CH%d: Error frame flood detected (%u/s), throttling", ch,
				mon->err_frame_count);
			mon->throttled = true;
			/* USB error and CAN error */
			status_led_usb_set(LED_USB_ERROR);
			status_led_can_set(CAN_LED_ERROR);
		}
		/* Actively enter Bus-Off to protect bus */
		if (!mon->forced_busoff) {
			LOG_ERR("CH%d: Forcing Bus-Off to prevent bus freeze", ch);
			can_stop(dev);
			mon->forced_busoff = true;
			return;
		}
	}

	/* Handle different error states */
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		LOG_DBG("CH%d: CAN ERROR_ACTIVE (TEC=%u, REC=%u)", ch, err_cnt.tx_err_cnt,
			err_cnt.rx_err_cnt);
		mon->err_passive_count = 0; /* Reset count after recovery */
		mon->forced_busoff = false;
		/* CAN back to normal */
		status_led_can_set(CAN_LED_ACTIVE);
		break;

	case CAN_STATE_ERROR_WARNING:
		LOG_WRN("CH%d: CAN ERROR_WARNING (TEC=%u, REC=%u)", ch, err_cnt.tx_err_cnt,
			err_cnt.rx_err_cnt);
		/* CAN warning state */
		status_led_can_set(CAN_LED_WARNING);
		break;

	case CAN_STATE_ERROR_PASSIVE:
		mon->err_passive_count++;
		LOG_WRN("CH%d: CAN ERROR_PASSIVE #%u (TEC=%u, REC=%u)", ch, mon->err_passive_count,
			err_cnt.tx_err_cnt, err_cnt.rx_err_cnt);
		/* CAN error state */
		status_led_can_set(CAN_LED_ERROR);

		/* Persistent ERROR_PASSIVE -> active Bus-Off */
		if (mon->err_passive_count > CAN_ERR_PASSIVE_LIMIT) {
			LOG_ERR("CH%d: Persistent ERROR_PASSIVE (%u times), forcing Bus-Off", ch,
				mon->err_passive_count);
			can_stop(dev);
			mon->forced_busoff = true;
			/* USB error and CAN off */
			status_led_usb_set(LED_USB_ERROR);
			status_led_can_set(CAN_LED_OFF);
		}
		break;

	case CAN_STATE_BUS_OFF:
		LOG_ERR("CH%d: CAN BUS_OFF (TEC=%u, REC=%u)", ch, err_cnt.tx_err_cnt,
			err_cnt.rx_err_cnt);
		mon->forced_busoff = true;
		/* USB error and CAN off */
		status_led_usb_set(LED_USB_ERROR);
		status_led_can_set(CAN_LED_OFF);
		break;

	case CAN_STATE_STOPPED:
		LOG_INF("CH%d: CAN STOPPED", ch);
		/* CAN stopped */
		status_led_can_set(CAN_LED_OFF);
		break;
	}
}

/**
 * @brief Main application entry point
 *
 * Initializes the roboto_usb2can adapter including:
 * - Status LED system
 * - CAN error monitoring
 * - GS-USB protocol stack
 * - USB device configuration (WinUSB support)
 *
 * @return 0 on success, negative error code on failure
 */
int main(void)
{
	const struct device *gs_usb = DEVICE_DT_GET(DT_NODELABEL(gs_usb0));
	const struct device *channels[] = {DEVICE_DT_GET(DT_NODELABEL(fdcan1))};
	struct gs_usb_ops ops = {
		.event = status_led_event,
	};
	int err;

	/* Initialize status LED */
	err = status_led_init();
	if (err) {
		LOG_ERR("Failed to initialize status LED (err %d)", err);
	}

	printk("*** roboto_usb2can adapter v%s ***\n", APP_VERSION_STR);

	/* Initialize CAN error monitoring */
	for (int i = 0; i < ARRAY_SIZE(can_devices); i++) {
		if (!device_is_ready(can_devices[i])) {
			LOG_ERR("CAN device %d not ready", i);
			continue;
		}

		/* Register CAN state change callback */
		can_set_state_change_callback(can_devices[i], can_state_change_callback,
					      (void *)(intptr_t)i);
		LOG_INF("CAN error monitoring enabled for channel %d", i);

		/* Initialize error monitor */
		err_monitors[i].window_start_ms = k_uptime_get();
	}

	if (!device_is_ready(gs_usb)) {
		LOG_ERR("gs_usb not ready");
		return -1;
	}

	/* Register gs_usb device and channels, and set event callback */
	err = gs_usb_register(gs_usb, channels, ARRAY_SIZE(channels), &ops, NULL);
	if (err != 0) {
		LOG_ERR("failed to register gs_usb (err %d)", err);
		return err;
	}

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
	/* New USB stack initialization */
	err = usbd_add_descriptor(&usbd, &lang);
	if (err != 0) {
		LOG_ERR("failed to add language descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &mfr);
	if (err != 0) {
		LOG_ERR("failed to add manufacturer descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &product);
	if (err != 0) {
		LOG_ERR("failed to add product descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &sn);
	if (err != 0) {
		LOG_ERR("failed to add serial number descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_configuration(&usbd, USBD_SPEED_FS, &fs_config);
	if (err != 0) {
		LOG_ERR("failed to add full-speed configuration (err %d)", err);
		return err;
	}

	err = usbd_register_class(&usbd, "gs_usb_0", USBD_SPEED_FS, 1);
	if (err != 0) {
		LOG_ERR("failed to register gs_usb class (err %d)", err);
		return err;
	}

	err = usbd_device_set_code_triple(&usbd, USBD_SPEED_FS, 0, 0, 0);
	if (err != 0) {
		LOG_ERR("failed to set code triple (err %d)", err);
		return err;
	}

	/* Set USB version to 2.0.1 to trigger BOS descriptor read (required for WinUSB) */
	err = usbd_device_set_bcd_usb(&usbd, USBD_SPEED_FS, USB_SRN_2_0_1);
	if (err != 0) {
		LOG_ERR("failed to set FS bcdUSB (err %d)", err);
		return err;
	}

	/* Set device version */
	err = usbd_device_set_bcd_device(&usbd, APP_VERSION_BCD);
	if (err != 0) {
		LOG_ERR("failed to set bcdDevice (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &bos_lpm);
	if (err != 0) {
		LOG_ERR("failed to add BOS LPM descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &bos_msosv2);
	if (err != 0) {
		LOG_ERR("failed to add BOS MSOS2 descriptor (err %d)", err);
		return err;
	}

	err = usbd_init(&usbd);
	if (err != 0) {
		LOG_ERR("failed to initialize USB device (err %d)", err);
		return err;
	}

	err = usbd_enable(&usbd);
	if (err != 0) {
		LOG_ERR("failed to enable USB device (err %d)", err);
		return err;
	}
#else
	/* Old USB stack */
	err = usb_enable(NULL);
	if (err != 0) {
		LOG_ERR("failed to enable USB (err %d)", err);
		return err;
	}
#endif

	LOG_INF("roboto_usb2can initialized with %u channels", ARRAY_SIZE(channels));

	/* Set LED to USB ready state */
	status_led_usb_set(LED_USB_READY);
	LOG_INF("WinUSB support enabled - plug and play on Windows 8.1+");

	return 0;
}
