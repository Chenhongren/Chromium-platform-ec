/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "dps.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "system.h"
#include "task.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"

#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/slist.h>
#include <zephyr/ztest.h>

#define BATTERY_NODE DT_NODELABEL(battery)

#define GPIO_BATT_PRES_ODL_PATH NAMED_GPIOS_GPIO_NODE(ec_batt_pres_odl)
#define GPIO_BATT_PRES_ODL_PORT DT_GPIO_PIN(GPIO_BATT_PRES_ODL_PATH, gpios)

#define TEST_PORT 0

struct usb_attach_5v_3a_pd_source_fixture {
	struct tcpci_partner_data source_5v_3a;
	struct tcpci_src_emul_data src_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *usb_attach_5v_3a_pd_source_setup(void)
{
	static struct usb_attach_5v_3a_pd_source_fixture test_fixture;

	/* Get references for the emulators */
	test_fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	test_fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	/* Initialized the charger to supply 5V and 3A */
	tcpci_partner_init(&test_fixture.source_5v_3a, PD_REV20);
	test_fixture.source_5v_3a.extensions = tcpci_src_emul_init(
		&test_fixture.src_ext, &test_fixture.source_5v_3a, NULL);
	test_fixture.src_ext.pdo[1] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &test_fixture;
}

static void usb_attach_5v_3a_pd_source_before(void *data)
{
	struct usb_attach_5v_3a_pd_source_fixture *fixture = data;

	connect_source_to_port(&fixture->source_5v_3a, &fixture->src_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);
}

static void usb_attach_5v_3a_pd_source_after(void *data)
{
	struct usb_attach_5v_3a_pd_source_fixture *fixture = data;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
}

static void control_battery_present(bool present)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));

	/* 0 means battery present */
	zassert_ok(gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, !present));
}

ZTEST_SUITE(usb_attach_5v_3a_pd_source, drivers_predicate_post_main,
	    usb_attach_5v_3a_pd_source_setup, usb_attach_5v_3a_pd_source_before,
	    usb_attach_5v_3a_pd_source_after, NULL);

ZTEST(usb_attach_5v_3a_pd_source, test_battery_is_charging)
{
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	uint16_t battery_status;

	zassert_ok(sbat_emul_get_word_val(emul, SB_BATTERY_STATUS,
					  &battery_status));
	zassert_equal(battery_status & STATUS_DISCHARGING, 0,
		      "Battery is discharging: %d", battery_status);
}

ZTEST(usb_attach_5v_3a_pd_source, test_charge_state)
{
	struct ec_response_charge_state state =
		host_cmd_charge_state(TEST_PORT);

	zassert_true(state.get_state.ac, "AC_OK not triggered");
	zassert_true(state.get_state.chg_voltage > 0,
		     "Expected a charge voltage, but got %dmV",
		     state.get_state.chg_voltage);
	zassert_true(state.get_state.chg_current > 0,
		     "Expected a charge current, but got %dmA",
		     state.get_state.chg_current);
}

ZTEST(usb_attach_5v_3a_pd_source, test_typec_status)
{
	struct ec_response_typec_status status = host_cmd_typec_status(0);

	zassert_true(status.pd_enabled, "PD is disabled");
	zassert_true(status.dev_connected, "Device disconnected");
	zassert_true(status.sop_connected, "Charger is not SOP capable");
	zassert_equal(status.source_cap_count, 2,
		      "Expected 2 source PDOs, but got %d",
		      status.source_cap_count);
	zassert_equal(status.power_role, PD_ROLE_SINK,
		      "Expected power role to be %d, but got %d", PD_ROLE_SINK,
		      status.power_role);
}

ZTEST(usb_attach_5v_3a_pd_source, test_power_info)
{
	struct ec_response_usb_pd_power_info info = host_cmd_power_info(0);

	zassert_equal(info.role, USB_PD_PORT_POWER_SINK,
		      "Expected role to be %d, but got %d",
		      USB_PD_PORT_POWER_SINK, info.role);
	zassert_equal(info.type, USB_CHG_TYPE_PD,
		      "Expected type to be %d, but got %d", USB_CHG_TYPE_PD,
		      info.type);
	zassert_equal(info.meas.voltage_max, 5000,
		      "Expected charge voltage max of 5000mV, but got %dmV",
		      info.meas.voltage_max);
	zassert_within(
		info.meas.voltage_now, 5000, 500,
		"Charging voltage expected to be near 5000mV, but was %dmV",
		info.meas.voltage_now);
	zassert_equal(info.meas.current_max, 3000,
		      "Current max expected to be 3000mV, but was %dmV",
		      info.meas.current_max);
	zassert_true(info.meas.current_lim >= 3000,
		     "VBUS max is set to 3000mA, but PD is reporting %dmA",
		     info.meas.current_lim);
	zassert_equal(info.max_power, 5000 * 3000,
		      "Charging expected to be at %duW, but PD max is %duW",
		      5000 * 3000, info.max_power);
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_disconnect_battery_not_charging)
{
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	uint16_t battery_status;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	zassert_ok(sbat_emul_get_word_val(emul, SB_BATTERY_STATUS,
					  &battery_status));
	zassert_equal(battery_status & STATUS_DISCHARGING, STATUS_DISCHARGING,
		      "Battery is not discharging: %d", battery_status);
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_disconnect_charge_state)
{
	struct ec_response_charge_state state;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	state = host_cmd_charge_state(TEST_PORT);

	zassert_false(state.get_state.ac, "AC_OK not triggered");
	zassert_equal(state.get_state.chg_current, 0,
		      "Max charge current expected 0mA, but was %dmA",
		      state.get_state.chg_current);
	zassert_equal(state.get_state.chg_input_current,
		      CONFIG_PLATFORM_EC_CHARGER_DEFAULT_CURRENT_LIMIT,
		      "Charge input current limit expected %dmA, but was %dmA",
		      CONFIG_PLATFORM_EC_CHARGER_DEFAULT_CURRENT_LIMIT,
		      state.get_state.chg_input_current);
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_disconnect_typec_status)
{
	struct ec_response_typec_status typec_status;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	typec_status = host_cmd_typec_status(0);

	zassert_false(typec_status.pd_enabled);
	zassert_false(typec_status.dev_connected);
	zassert_false(typec_status.sop_connected);
	zassert_equal(typec_status.source_cap_count, 0,
		      "Expected 0 source caps, but got %d",
		      typec_status.source_cap_count);
	zassert_equal(typec_status.power_role, USB_CHG_TYPE_NONE,
		      "Expected power role to be %d, but got %d",
		      USB_CHG_TYPE_NONE, typec_status.power_role);
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_disconnect_power_info)
{
	struct ec_response_usb_pd_power_info power_info;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	power_info = host_cmd_power_info(0);

	zassert_equal(power_info.role, USB_PD_PORT_POWER_DISCONNECTED,
		      "Expected power role to be %d, but got %d",
		      USB_PD_PORT_POWER_DISCONNECTED, power_info.role);
	zassert_equal(power_info.type, USB_CHG_TYPE_NONE,
		      "Expected charger type to be %d, but got %s",
		      USB_CHG_TYPE_NONE, power_info.type);
	zassert_equal(power_info.max_power, 0,
		      "Expected the maximum power to be 0uW, but got %duW",
		      power_info.max_power);
	zassert_equal(power_info.meas.voltage_max, 0,
		      "Expected maximum voltage of 0mV, but got %dmV",
		      power_info.meas.voltage_max);
	zassert_within(power_info.meas.voltage_now, 5, 5,
		       "Expected present voltage near 0mV, but got %dmV",
		       power_info.meas.voltage_now);
	zassert_equal(power_info.meas.current_max, 0,
		      "Expected maximum current of 0mA, but got %dmA",
		      power_info.meas.current_max);
	zassert_true(power_info.meas.current_lim >= 0,
		     "Expected the PD current limit to be >= 0, but got %dmA",
		     power_info.meas.current_lim);
}

ZTEST(usb_attach_5v_3a_pd_source,
      test_ap_can_boot_on_low_battery_while_charging)
{
	const struct emul *smart_batt_emul = EMUL_DT_GET(DT_NODELABEL(battery));
	struct sbat_emul_bat_data *batt_data =
		sbat_emul_get_bat_data(smart_batt_emul);

	/* Set capacity to what gives a charge percentage less than required
	 * for booting the AP
	 *
	 * Capacaity is reset by emulator's ZTEST_RULE
	 */
	batt_data->cap = (CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON *
			  batt_data->design_cap / 100) -
			 1;

	zassert_true(system_can_boot_ap());
}

ZTEST_F(usb_attach_5v_3a_pd_source,
	test_ap_fails_to_boot_on_low_battery_while_not_charging)
{
	const struct emul *smart_batt_emul = EMUL_DT_GET(DT_NODELABEL(battery));
	struct sbat_emul_bat_data *batt_data =
		sbat_emul_get_bat_data(smart_batt_emul);

	/* Set capacity to what gives a charge percentage less than required
	 * for booting the AP
	 *
	 * Capacaity is reset by emulator's ZTEST_RULE
	 */
	batt_data->cap = (CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON *
			  batt_data->design_cap / 100) -
			 1;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);

	zassert_false(system_can_boot_ap());
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_uvdm_ignored)
{
	uint32_t vdm_header = VDO(USB_VID_GOOGLE, 0 /* unstructured */, 0);

	tcpci_partner_common_enable_pd_logging(&fixture->source_5v_3a, true);
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_VENDOR_DEF,
				    &vdm_header, 1, 0);
	k_sleep(K_SECONDS(1));
	tcpci_partner_common_enable_pd_logging(&fixture->source_5v_3a, false);

	bool tcpm_response = false;
	struct tcpci_partner_log_msg *msg;

	/* The TCPM does not support any unstructured VDMs. In PD 2.0, it should
	 * ignore them.
	 */

	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->source_5v_3a.msg_log, msg, node)
	{
		/* Ignore messages from the port partner */
		if (msg->sender == TCPCI_PARTNER_SENDER_PARTNER) {
			continue;
		}

		if (msg->sender == TCPCI_PARTNER_SENDER_TCPM) {
			tcpm_response = true;
			break;
		}
	}

	zassert_false(tcpm_response,
		      "Sent unstructured VDM to TCPM; TCPM did not ignore");
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_dps_battery_absent)
{
	control_battery_present(false);
	zassert_false(battery_is_present(), "dps battery is present");
	task_wake(TASK_ID_DPS);
	/* wait dps_config.t_check*/
	k_sleep(K_MSEC(5000));
	zassert_true(dps_get_flag() & DPS_FLAG_NO_BATTERY,
		     "DPS_FLAG_NO_BATTERY is set");
	control_battery_present(true);
	zassert_true(battery_is_present(), "dps battery is not present");
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_dps_enable)
{
	dps_enable(false);
	zassert_false(dps_is_enabled());
	task_wake(TASK_ID_DPS);
	/* wait dps_config.t_check*/
	k_sleep(K_MSEC(5000));
	zassert_true(dps_get_flag() & DPS_FLAG_DISABLED,
		     "DPS_FLAG_DISABLED is set");
	dps_enable(true);
	zassert_true(dps_is_enabled());
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_dps_info)
{
	const struct shell *shell_zephyr = get_ec_shell();

	const char *outbuffer;
	uint32_t buffer_size;
	/* Arbitrary array size for sprintf should not need this amount */
	char format_buffer[100];

	shell_backend_dummy_clear_output(shell_zephyr);

	/* Print current status to console */
	zassert_ok(shell_execute_cmd(shell_zephyr, "dps"), NULL);
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	/* Should include extra information about the charging port */
	/* Charging Port */
	sprintf(format_buffer, "C%d", TEST_PORT);
	zassert_not_null(strstr(outbuffer, format_buffer));
	/* We are a sink to a 5v3a source, so check requested mv/ma */
	sprintf(format_buffer, "Requested: %dmV/%dmA", 5000, 3000);
	zassert_not_null(strstr(outbuffer, format_buffer));
	/*
	 * Measured input power is shown (values vary so not asserting on
	 * numbers)
	 */
	zassert_not_null(strstr(outbuffer, "Measured:"));
	/* Efficient Voltage - Value varies based on battery*/
	zassert_not_null(strstr(outbuffer, "Efficient:"));
	/* Battery Design Voltage (varies based on battery) */
	zassert_not_null(strstr(outbuffer, "Batt:"));
	/* PDMaxMV */
	sprintf(format_buffer, "PDMaxMV:   %dmV", pd_get_max_voltage());
	zassert_not_null(strstr(outbuffer, format_buffer));
}
