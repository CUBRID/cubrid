/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */


/*
 * arithmetic.h: interface for arithmetic functions
 */

#ifndef _ARITHMETIC_H_
#define _ARITHMETIC_H_

#ident "$Id$"

#include "dbtype_def.h"
#include "object_domain.h"
#include <vector>
#include <string>

#define PI ((double) (3.14159265358979323846264338))

extern int db_floor_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_ceil_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_sign_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_abs_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_exp_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_sqrt_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_power_dbval (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2);
extern int db_mod_dbval (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2);
extern int db_round_dbval (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2);
extern int db_log_dbval (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2);
extern int db_trunc_dbval (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2);
extern int db_random_dbval (DB_VALUE * result);
extern int db_drandom_dbval (DB_VALUE * result);
extern int db_bit_count_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_cos_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_sin_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_tan_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_cot_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_acos_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_asin_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_atan_dbval (DB_VALUE * result, DB_VALUE * value1);
extern int db_atan2_dbval (DB_VALUE * result, DB_VALUE * value, DB_VALUE * value2);
extern int db_degrees_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_radians_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_log_generic_dbval (DB_VALUE * result, DB_VALUE * value, long b);
extern int db_typeof_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_width_bucket (DB_VALUE * result, const DB_VALUE * value1, const DB_VALUE * value2,
			    const DB_VALUE * value3, const DB_VALUE * value4);
/* temporarily put db_sleep here. A better choice is introduce a new file system_opfunc.c
 * But currently, there is just one such functions.
 */
extern int db_sleep (DB_VALUE * result, DB_VALUE * value);
extern int db_crc32_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_least_or_greatest (DB_VALUE * arg1, DB_VALUE * arg2, DB_VALUE * result, bool least);

// json functions
extern int db_accumulate_json_arrayagg (const DB_VALUE * json_db_val, DB_VALUE * json_res);
extern int db_accumulate_json_objectagg (const DB_VALUE * json_key, const DB_VALUE * json_db_val, DB_VALUE * json_res);
extern int db_evaluate_json_array (DB_VALUE * result, DB_VALUE * const *arg, int const num_args);
extern int db_evaluate_json_array_append (DB_VALUE * result, DB_VALUE * const *arg, int const num_args);
extern int db_evaluate_json_array_insert (DB_VALUE * result, DB_VALUE * const *arg, int const num_args);
extern int db_evaluate_json_contains (DB_VALUE * result, DB_VALUE * const *arg, int const num_args);
extern int db_evaluate_json_contains_path (DB_VALUE * result, DB_VALUE * const *arg, const int num_args);
extern int db_evaluate_json_depth (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_json_extract (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_json_get_all_paths (DB_VALUE * result, DB_VALUE * const *arg, int const num_args);
extern int db_evaluate_json_insert (DB_VALUE * result, DB_VALUE * const *arg, const int num_args);
extern int db_evaluate_json_keys (DB_VALUE * result, DB_VALUE * const *arg, const int num_args);
extern int db_evaluate_json_length (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_json_merge_preserve (DB_VALUE * result, DB_VALUE * const *arg, int const num_args);
extern int db_evaluate_json_merge_patch (DB_VALUE * result, DB_VALUE * const *arg, int const num_args);
extern int db_evaluate_json_object (DB_VALUE * result, DB_VALUE * const *arg, int const num_args);
extern int db_evaluate_json_pretty (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_json_quote (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_json_remove (DB_VALUE * result, DB_VALUE * const *arg, int const num_args);
extern int db_evaluate_json_replace (DB_VALUE * result, DB_VALUE * const *arg, const int num_args);
extern int db_evaluate_json_search (DB_VALUE * result, DB_VALUE * const *args, const int num_args);
extern int db_evaluate_json_set (DB_VALUE * result, DB_VALUE * const *arg, const int num_args);
extern int db_evaluate_json_type_dbval (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_json_unquote (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_json_valid (DB_VALUE * result, DB_VALUE * const *args, int num_args);

#endif /* _ARITHMETIC_H_ */
