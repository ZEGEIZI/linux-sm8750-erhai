// SPDX-License-Identifier: GPL-2.0-only
/* Host test of the actual PPS policy helpers; no devices or firmware needed. */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>

#include "../kselftest.h"
#include "../../../../drivers/power/supply/qcom_battmgr_pps.h"

static void check_curves(void)
{
	static const struct {
		int soc, temp, mv, ma;
	} cases[] = {
		{ 20, 100, 4000, 3000 },
		{ 35, 100, 4000, 3000 },
		{ 36, 100, 4000, 2500 },
		{ 56, 180, 4000, 2000 },
		{ 76, 180, 4000, 1500 },
		{ 86, 180, 4000, 1000 },
		{ 10, 100, 4200, 2500 },
		{ 10, 250, 4380, 2950 },
		{ 10, 250, 4480, 1000 },
		{ 10, 250, 4490, 0 },
		{ 10, 400, 4470, 0 },
		{ 10, 450, 4130, 0 },
		{ 90, 250, 4000, 0 },
		{ 10, -1, 4000, 0 },
		{ 10, 510, 4000, 0 },
		{ 10, 250, -EIO, 0 },
		{ 10, 250, 0, 0 },
	};
	unsigned int i;
	bool ok = true;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		int got = opd_pps_curve_limit(cases[i].soc, cases[i].temp, cases[i].mv);

		if (got != cases[i].ma) {
			ksft_print_msg("curve case %u: %d != %d\n", i, got, cases[i].ma);
			ok = false;
		}
	}
	ksft_test_result(ok, "stock temperature/SOC/voltage boundaries\n");
}

static void check_limits(void)
{
	int soc, temp, mv, cap, last;
	bool bounded = true, descending = true;

	for (soc = 0; soc <= 100; soc++) {
		for (temp = -10; temp <= 520; temp += 10) {
			last = OPD_PPS_MAX_MA;
			for (mv = 3500; mv <= 4550; mv += 10) {
				cap = opd_pps_current(soc, temp, mv, 5000, last);
				bounded &= cap >= 0 && cap <= OPD_PPS_MAX_MA && cap % 50 == 0;
				descending &= cap <= last;
				last = cap;
			}
		}
	}
	ksft_test_result(bounded && descending, "curve descent and hard 3 A ceiling\n");
	ksft_test_result(opd_pps_current(10, 250, 4000, 1820, 3000) == 1800 &&
			opd_pps_current(10, 250, 4000, 3000, 1000) == 1000 &&
			opd_pps_current(10, 250, 4000, -EIO, 3000) == 0,
			"source clamp, PPS granularity and no recovery to a higher curve\n");
	ksft_test_result(opd_pps_step_ma(1000, 3000) == 1500 &&
			opd_pps_step_ma(2500, 2800) == 2800 &&
			opd_pps_step_ma(2800, 2950) == 2900 &&
			opd_pps_step_ma(2900, 2950) == 2950 &&
			opd_pps_step_ma(3000, 1000) == 1000 &&
			opd_pps_step_ma(INT_MIN, 3000) == 0 &&
			opd_pps_step_ma(1000, 5000) == 0,
			"stock current ramp and immediate derating\n");
}

static void check_servo(void)
{
	ksft_test_result(opd_pps_servo_mv(8400, 3900, 1500, 2000) == 8440 &&
			opd_pps_servo_mv(8400, 3900, 2200, 2000) == 8360 &&
			opd_pps_servo_mv(8400, 3900, 2600, 2000) == 8300 &&
			opd_pps_servo_mv(8400, 3900, 2000, 2000) == 8400,
			"40 mV servo, 100 mV overcurrent step and deadband\n");
	ksft_test_result(opd_pps_servo_mv(8400, -EIO, 1000, 3000) == 0 &&
			opd_pps_servo_mv(8400, 3900, -EIO, 3000) == 0 &&
			opd_pps_servo_mv(8400, 3900, 0, 3000) == 8400 &&
			opd_pps_servo_mv(8400, 3900, 3100, 3000) == 0 &&
			opd_pps_servo_mv(8400, 3900, 1000, 5000) == 0,
			"invalid ADC, disconnected pump and excessive current fail closed\n");
	ksft_test_result(opd_pps_servo_mv(9500, 4400, 1000, 3000) == 9500 &&
			opd_pps_servo_mv(7920, 3900, 2500, 2000) == 0 &&
			opd_pps_entry_mv(3900) == 8250 && opd_pps_entry_mv(-EIO) == 0 &&
			opd_pps_entry_mv(INT_MAX) == 0 &&
			opd_pps_ramp_mv(5500, 5500, 8250) == 7500,
			"entry window, coarse ramp and voltage headroom\n");
}

static void check_exits(void)
{
	ksft_test_result(opd_pps_check(40, 250, 3900, 5500, 8400, 2800, 90) == OPD_PPS_OK &&
			opd_pps_check(40, 321, 3900, 5500, 8400, 2800, 90) == OPD_PPS_TEMPERATURE &&
			opd_pps_check(40, 49, 3900, 5500, 8400, 2800, 90) == OPD_PPS_TEMPERATURE &&
			opd_pps_check(80, 250, 3900, 5500, 8400, 2800, 80) == OPD_PPS_SOC &&
			opd_pps_check(90, 250, 3900, 5500, 8400, 2800, 100) == OPD_PPS_SOC,
			"temperature and charge-control SOC exits\n");
	ksft_test_result(opd_pps_check(40, 250, 4490, 5500, 9400, 2800, 90) == OPD_PPS_FULL &&
			opd_pps_check(40, 250, 4540, 5500, 9400, 2800, 90) == OPD_PPS_FULL &&
			opd_pps_check(40, 250, 3900, 7400, 8400, 2800, 90) ==
				OPD_PPS_BATTERY_CURRENT &&
			opd_pps_check(40, 250, 3900, 5500, 8400, 3001, 90) ==
				OPD_PPS_INPUT_CURRENT &&
			opd_pps_check(40, 250, 3900, 5500, 10000, 2800, 90) ==
				OPD_PPS_INPUT_VOLTAGE &&
			opd_pps_check(40, 250, -EIO, 5500, 8400, 2800, 90) == OPD_PPS_INVALID,
			"full-voltage, battery/bus overcurrent and invalid-voltage exits\n");
}

int main(void)
{
	ksft_print_header();
	ksft_set_plan(9);
	check_curves();
	check_limits();
	check_servo();
	check_exits();
	ksft_finished();
}
