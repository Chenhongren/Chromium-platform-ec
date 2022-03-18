/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <kernel.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/emul.h>
#include <ztest.h>
#include <fff.h>

#include "driver/ppc/sn5s330.h"
#include "driver/ppc/sn5s330_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_sn5s330.h"
#include "usbc_ppc.h"
#include "test_mocks.h"
#include "test_state.h"

/** This must match the index of the sn5s330 in ppc_chips[] */
#define SN5S330_PORT 0
#define EMUL emul_get_binding(DT_LABEL(DT_NODELABEL(sn5s330_emul)))
#define FUNC_SET1_ILIMPP1_MSK 0x1F
#define SN5S330_INTERRUPT_DELAYMS 15

FAKE_VOID_FUNC(sn5s330_emul_interrupt_set_stub);

/*
 * TODO(b/203364783): Exclude other threads from interacting with the emulator
 * to avoid test flakiness
 */

struct intercept_write_data {
	int reg_to_intercept;
	uint8_t val_intercepted;
};

struct intercept_read_data {
	int reg_to_intercept;
	bool replace_reg_val;
	uint8_t replacement_val;
};

static int intercept_read_func(struct i2c_emul *emul, int reg, uint8_t *val,
			       int bytes, void *data)
{
	struct intercept_read_data *test_data = data;

	if (test_data->reg_to_intercept && test_data->replace_reg_val)
		*val = test_data->replacement_val;

	return EC_SUCCESS;
}

static int intercept_write_func(struct i2c_emul *emul, int reg, uint8_t val,
				int bytes, void *data)
{
	struct intercept_write_data *test_data = data;

	if (test_data->reg_to_intercept == reg)
		test_data->val_intercepted = val;

	return 1;
}

static int fail_until_write_func(struct i2c_emul *emul, int reg, uint8_t val,
				 int bytes, void *data)
{
	uint32_t *count = data;

	if (*count != 0) {
		*count -= 1;
		return -EIO;
	}
	return 1;
}

ZTEST(ppc_sn5s330, test_fail_once_func_set1)
{
	const struct emul *emul = EMUL;
	struct i2c_emul *i2c_emul = sn5s330_emul_to_i2c_emul(emul);
	uint32_t count = 1;
	uint8_t func_set1_value;

	i2c_common_emul_set_write_func(i2c_emul, fail_until_write_func, &count);

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);
	zassert_equal(count, 0, NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET1, &func_set1_value);
	zassert_true((func_set1_value & SN5S330_ILIM_1_62) != 0, NULL);
	i2c_common_emul_set_write_func(i2c_emul, NULL, NULL);
}

ZTEST(ppc_sn5s330, test_dead_battery_boot_force_pp2_fets_set)
{
	const struct emul *emul = EMUL;
	struct i2c_emul *i2c_emul = sn5s330_emul_to_i2c_emul(emul);
	struct intercept_write_data test_write_data = {
		.reg_to_intercept = SN5S330_FUNC_SET3,
		.val_intercepted = 0,
	};
	struct intercept_read_data test_read_data = {
		.reg_to_intercept = SN5S330_INT_STATUS_REG4,
		.replace_reg_val = true,
		.replacement_val = SN5S330_DB_BOOT,
	};

	i2c_common_emul_set_write_func(i2c_emul, intercept_write_func,
				       &test_write_data);
	i2c_common_emul_set_read_func(i2c_emul, intercept_read_func,
				      &test_read_data);

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	/*
	 * Although the device enables PP2_FET on dead battery boot by setting
	 * the PP2_EN bit, the driver also force sets this bit during dead
	 * battery boot by writing that bit to the FUNC_SET3 reg.
	 *
	 * TODO(b/207034759): Verify need or remove redundant PP2 set.
	 */
	zassert_true(test_write_data.val_intercepted & SN5S330_PP2_EN, NULL);
	zassert_false(sn5s330_drv.is_sourcing_vbus(SN5S330_PORT), NULL);
}

ZTEST(ppc_sn5s330, test_enter_low_power_mode)
{
	const struct emul *emul = EMUL;

	uint8_t func_set2_reg;
	uint8_t func_set3_reg;
	uint8_t func_set4_reg;
	uint8_t func_set9_reg;

	/*
	 * Requirements were extracted from TI's recommended changes for octopus
	 * to lower power use during hibernate as well as the follow up changes
	 * we made to allow the device to wake up from hibernate.
	 *
	 * For Reference: b/111006203#comment35
	 */

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);
	zassert_ok(sn5s330_drv.enter_low_power_mode(SN5S330_PORT), NULL);

	/* 1) Verify VBUS power paths are off */
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_equal(func_set3_reg & SN5S330_PP1_EN, 0, NULL);
	zassert_equal(func_set3_reg & SN5S330_PP2_EN, 0, NULL);

	/* 2) Verify VCONN power path is off */
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET4, &func_set4_reg);
	zassert_not_equal(func_set4_reg & SN5S330_CC_EN, 0, NULL);
	zassert_equal(func_set4_reg & SN5S330_VCONN_EN, 0, NULL);

	/* 3) Verify SBU FET is off */
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET2, &func_set2_reg);
	zassert_equal(func_set2_reg & SN5S330_SBU_EN, 0, NULL);

	/* 4) Verify VBUS and SBU OVP comparators are off */
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET9, &func_set9_reg);
	zassert_equal(func_set9_reg & SN5S330_FORCE_OVP_EN_SBU, 0, NULL);
	zassert_equal(func_set9_reg & SN5S330_PWR_OVR_VBUS, 0, NULL);
	zassert_not_equal(func_set9_reg & SN5S330_OVP_EN_CC, 0, NULL);
	zassert_equal(func_set9_reg & SN5S330_FORCE_ON_VBUS_OVP, 0, NULL);
	zassert_equal(func_set9_reg & SN5S330_FORCE_ON_VBUS_UVP, 0, NULL);
}

ZTEST(ppc_sn5s330, test_vbus_source_sink_enable)
{
	const struct emul *emul = EMUL;
	uint8_t func_set3_reg;

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	/* Test enable/disable VBUS source FET */
	zassert_ok(sn5s330_drv.vbus_source_enable(SN5S330_PORT, true), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_not_equal(func_set3_reg & SN5S330_PP1_EN, 0, NULL);

	zassert_ok(sn5s330_drv.vbus_source_enable(SN5S330_PORT, false), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_equal(func_set3_reg & SN5S330_PP1_EN, 0, NULL);

	/* Test enable/disable VBUS sink FET */
	zassert_ok(sn5s330_drv.vbus_sink_enable(SN5S330_PORT, true), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_not_equal(func_set3_reg & SN5S330_PP2_EN, 0, NULL);

	zassert_ok(sn5s330_drv.vbus_sink_enable(SN5S330_PORT, false), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_equal(func_set3_reg & SN5S330_PP2_EN, 0, NULL);
}

/* This test depends on EC GIPO initialization happening before I2C */
BUILD_ASSERT(
	CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY < CONFIG_I2C_INIT_PRIORITY,
	"GPIO initialization must happen before I2C");
ZTEST(ppc_sn5s330, test_vbus_discharge)
{
	const struct emul *emul = EMUL;
	uint8_t func_set3_reg;

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	/* Test enable/disable VBUS discharging */
	zassert_ok(sn5s330_drv.discharge_vbus(SN5S330_PORT, true), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_not_equal(func_set3_reg & SN5S330_VBUS_DISCH_EN, 0, NULL);

	zassert_ok(sn5s330_drv.discharge_vbus(SN5S330_PORT, false), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_equal(func_set3_reg & SN5S330_VBUS_DISCH_EN, 0, NULL);
}

ZTEST(ppc_sn5s330, test_set_vbus_source_current_limit)
{
	const struct emul *emul = EMUL;
	uint8_t func_set1_reg;

	/* Test every TCPC Pull Resistance Value */
	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	/* USB */
	zassert_ok(sn5s330_drv.set_vbus_source_current_limit(SN5S330_PORT,
							     TYPEC_RP_USB),
		   NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET1, &func_set1_reg);
	zassert_equal(func_set1_reg & FUNC_SET1_ILIMPP1_MSK, SN5S330_ILIM_0_63,
		      NULL);

	/* 1.5A */
	zassert_ok(sn5s330_drv.set_vbus_source_current_limit(SN5S330_PORT,
							     TYPEC_RP_1A5),
		   NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET1, &func_set1_reg);
	zassert_equal(func_set1_reg & FUNC_SET1_ILIMPP1_MSK, SN5S330_ILIM_1_62,
		      NULL);

	/* 3.0A */
	zassert_ok(sn5s330_drv.set_vbus_source_current_limit(SN5S330_PORT,
							     TYPEC_RP_3A0),
		   NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET1, &func_set1_reg);
	zassert_equal(func_set1_reg & FUNC_SET1_ILIMPP1_MSK, SN5S330_ILIM_3_06,
		      NULL);

	/* Unknown/Reserved - We set result as USB */
	zassert_ok(sn5s330_drv.set_vbus_source_current_limit(SN5S330_PORT,
							     TYPEC_RP_RESERVED),
		   NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET1, &func_set1_reg);
	zassert_equal(func_set1_reg & FUNC_SET1_ILIMPP1_MSK, SN5S330_ILIM_0_63,
		      NULL);
}

ZTEST(ppc_sn5s330, test_sn5s330_set_sbu)
#ifdef CONFIG_USBC_PPC_SBU
{
	const struct emul *emul = EMUL;
	uint8_t func_set2_reg;

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	/* Verify driver enables SBU FET */
	zassert_ok(sn5s330_drv.set_sbu(SN5S330_PORT, true), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET2, &func_set2_reg);
	zassert_not_equal(func_set2_reg & SN5S330_SBU_EN, 0, NULL);

	/* Verify driver disables SBU FET */
	zassert_ok(sn5s330_drv.set_sbu(SN5S330_PORT, false), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET2, &func_set2_reg);
	zassert_equal(func_set2_reg & SN5S330_SBU_EN, 0, NULL);
}
#else
{
	ztest_test_skip();
}
#endif /* CONFIG_USBC_PPC_SBU */

ZTEST(ppc_sn5s330, test_sn5s330_vbus_overcurrent)
{
	const struct emul *emul = EMUL;
	uint8_t int_trip_rise_reg1;

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	sn5s330_emul_make_vbus_overcurrent(emul);
	/*
	 * TODO(b/201420132): Replace arbitrary sleeps.
	 */
	/* Make sure interrupt happens first. */
	k_msleep(SN5S330_INTERRUPT_DELAYMS);
	zassert_true(sn5s330_emul_interrupt_set_stub_fake.call_count > 0, NULL);

	/*
	 * Verify we cleared vbus overcurrent interrupt trip rise bit so the
	 * driver can detect future overcurrent clamping interrupts.
	 */
	sn5s330_emul_peek_reg(emul, SN5S330_INT_TRIP_RISE_REG1,
			      &int_trip_rise_reg1);
	zassert_equal(int_trip_rise_reg1 & SN5S330_ILIM_PP1_MASK, 0, NULL);
}

ZTEST(ppc_sn5s330, test_sn5s330_disable_vbus_low_interrupt)
#ifdef CONFIG_USBC_PPC_VCONN
{
	const struct emul *emul = EMUL;

	/* Interrupt disabled here */
	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);
	/* Would normally cause a vbus low interrupt */
	sn5s330_emul_lower_vbus_below_minv(emul);
	zassert_equal(sn5s330_emul_interrupt_set_stub_fake.call_count, 0, NULL);
}
#else
{
	ztest_test_skip();
}
#endif /* CONFIG_USBC_PPC_VCONN */

ZTEST(ppc_sn5s330, test_sn5s330_set_vconn_fet)
{
	const struct emul *emul = EMUL;
	uint8_t func_set4_reg;

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	sn5s330_drv.set_vconn(SN5S330_PORT, false);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET4, &func_set4_reg);
	zassert_equal(func_set4_reg & SN5S330_VCONN_EN, 0, NULL);

	sn5s330_drv.set_vconn(SN5S330_PORT, true);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET4, &func_set4_reg);
	zassert_not_equal(func_set4_reg & SN5S330_VCONN_EN, 0, NULL);
}

/* Make an I2C emulator mock read func wrapped in FFF */
FAKE_VALUE_FUNC(int, dump_read_fn, struct i2c_emul *, int, uint8_t *, int,
		void *);

ZTEST(ppc_sn5s330, test_dump)
{
	int ret;
	struct i2c_emul *i2c_emul = sn5s330_emul_to_i2c_emul(EMUL);

	/* Set up our fake read function to pass through to the real emul */
	RESET_FAKE(dump_read_fn);
	dump_read_fn_fake.return_val = 1;
	i2c_common_emul_set_read_func(i2c_emul, dump_read_fn, NULL);

	ret = sn5s330_drv.reg_dump(SN5S330_PORT);

	zassert_equal(EC_SUCCESS, ret, "Expected EC_SUCCESS, got %d", ret);

	/* Check that all the expected I2C reads were performed. */
	MOCK_ASSERT_I2C_READ(dump_read_fn, 0, SN5S330_FUNC_SET1);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 1, SN5S330_FUNC_SET2);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 2, SN5S330_FUNC_SET3);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 3, SN5S330_FUNC_SET4);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 4, SN5S330_FUNC_SET5);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 5, SN5S330_FUNC_SET6);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 6, SN5S330_FUNC_SET7);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 7, SN5S330_FUNC_SET8);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 8, SN5S330_FUNC_SET9);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 9, SN5S330_FUNC_SET10);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 10, SN5S330_FUNC_SET11);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 11, SN5S330_FUNC_SET12);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 12, SN5S330_INT_STATUS_REG1);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 13, SN5S330_INT_STATUS_REG2);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 14, SN5S330_INT_STATUS_REG3);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 15, SN5S330_INT_STATUS_REG4);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 16, SN5S330_INT_TRIP_RISE_REG1);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 17, SN5S330_INT_TRIP_RISE_REG2);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 18, SN5S330_INT_TRIP_RISE_REG3);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 19, SN5S330_INT_TRIP_FALL_REG1);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 20, SN5S330_INT_TRIP_FALL_REG2);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 21, SN5S330_INT_TRIP_FALL_REG3);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 22, SN5S330_INT_MASK_RISE_REG1);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 23, SN5S330_INT_MASK_RISE_REG2);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 24, SN5S330_INT_MASK_RISE_REG3);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 25, SN5S330_INT_MASK_FALL_REG1);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 26, SN5S330_INT_MASK_FALL_REG2);
	MOCK_ASSERT_I2C_READ(dump_read_fn, 27, SN5S330_INT_MASK_FALL_REG3);
}

enum i2c_operation {
	I2C_WRITE,
	I2C_READ,
};

#define INIT_I2C_FAIL_HELPER(EMUL, RW, REG)                                \
	do {                                                               \
		if ((RW) == I2C_READ) {                                    \
			i2c_common_emul_set_read_fail_reg((EMUL), (REG));  \
			i2c_common_emul_set_write_fail_reg(                \
				(EMUL), I2C_COMMON_EMUL_NO_FAIL_REG);      \
		} else if ((RW) == I2C_WRITE) {                            \
			i2c_common_emul_set_read_fail_reg(                 \
				(EMUL), I2C_COMMON_EMUL_NO_FAIL_REG);      \
			i2c_common_emul_set_write_fail_reg((EMUL), (REG)); \
		} else {                                                   \
			zassert_true(false, "Invalid I2C operation");      \
		}                                                          \
		zassert_equal(                                             \
			EC_ERROR_INVAL, sn5s330_drv.init(SN5S330_PORT),    \
			"Did not get EC_ERROR_INVAL when reg %s (0x%02x)"  \
			"could not be %s",                                 \
			#REG, (REG),                                       \
			((RW) == I2C_READ) ? "read" : "written");          \
	} while (0)

ZTEST(ppc_sn5s330, test_init_reg_fails)
{
	struct i2c_emul *i2c_emul = sn5s330_emul_to_i2c_emul(EMUL);

	/* Fail on each of the I2C operations the init function does to ensure
	 * we get the correct return value. This includes operations made by
	 * clr_flags() and set_flags().
	 */

	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_FUNC_SET5);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_READ, SN5S330_FUNC_SET6);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_FUNC_SET6);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_FUNC_SET2);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_FUNC_SET9);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_FUNC_SET11);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_READ, SN5S330_FUNC_SET8);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_FUNC_SET8);

	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_READ, SN5S330_FUNC_SET4);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_FUNC_SET4);

	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_READ, SN5S330_FUNC_SET3);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_FUNC_SET3);

	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_READ, SN5S330_FUNC_SET10);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_FUNC_SET10);

	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_STATUS_REG4);

	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_MASK_RISE_REG1);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_MASK_FALL_REG1);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_MASK_RISE_REG2);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_MASK_FALL_REG2);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_MASK_RISE_REG3);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_MASK_FALL_REG3);

	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_READ, SN5S330_INT_STATUS_REG4);

	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_TRIP_RISE_REG1);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_TRIP_RISE_REG2);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_TRIP_RISE_REG3);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_TRIP_FALL_REG1);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_TRIP_FALL_REG2);
	INIT_I2C_FAIL_HELPER(i2c_emul, I2C_WRITE, SN5S330_INT_TRIP_FALL_REG3);
}

static inline void reset_sn5s330_state(void)
{
	struct i2c_emul *i2c_emul = sn5s330_emul_to_i2c_emul(EMUL);

	i2c_common_emul_set_write_func(i2c_emul, NULL, NULL);
	i2c_common_emul_set_read_func(i2c_emul, NULL, NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
		I2C_COMMON_EMUL_NO_FAIL_REG);
	sn5s330_emul_reset(EMUL);
	RESET_FAKE(sn5s330_emul_interrupt_set_stub);
}

static void ppc_sn5s330_before(void *state)
{
	ARG_UNUSED(state);
	reset_sn5s330_state();
}

static void ppc_sn5s330_after(void *state)
{
	ARG_UNUSED(state);
	reset_sn5s330_state();
}

ZTEST_SUITE(ppc_sn5s330, drivers_predicate_post_main, NULL, ppc_sn5s330_before,
	    ppc_sn5s330_after, NULL);
