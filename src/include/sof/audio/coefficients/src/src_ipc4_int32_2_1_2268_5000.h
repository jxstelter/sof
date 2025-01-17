/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 *
 */

/** \cond GENERATED_BY_TOOLS_TUNE_SRC */
#include <sof/audio/src/src.h>
#include <stdint.h>

const int32_t src_int32_2_1_2268_5000_fir[40] = {
	-138963,
	2521125,
	-7387024,
	5359642,
	19315854,
	-64900590,
	85876521,
	-859593,
	-266121446,
	885705351,
	1474154402,
	147338550,
	-233971827,
	133740271,
	-23793394,
	-26950829,
	25910619,
	-9304446,
	-66071,
	1055496,
	1055496,
	-66071,
	-9304446,
	25910619,
	-26950829,
	-23793394,
	133740271,
	-233971827,
	147338550,
	1474154402,
	885705351,
	-266121446,
	-859593,
	85876521,
	-64900590,
	19315854,
	5359642,
	-7387024,
	2521125,
	-138963

};

struct src_stage src_int32_2_1_2268_5000 = {
	0, 1, 2, 20, 40, 1, 2, 0, 0,
	src_int32_2_1_2268_5000_fir};
/** \endcond */
