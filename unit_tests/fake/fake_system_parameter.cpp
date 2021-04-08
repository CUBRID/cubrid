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

#include "system_parameter.h"

union fake_prmval
{
  int i;
  float f;
  bool b;
  char *str;
  int *ilist;
  UINT64 ull;
};

fake_prmval g_Prmdef[PRM_LAST_ID];

int
prm_get_integer_value (PARAM_ID prm_id)
{
  return g_Prmdef[prm_id].i;
}

float
prm_get_float_value (PARAM_ID prm_id)
{
  return g_Prmdef[prm_id].f;
}

bool
prm_get_bool_value (PARAM_ID prm_id)
{
  return g_Prmdef[prm_id].b;
}

char *
prm_get_string_value (PARAM_ID prm_id)
{
  return g_Prmdef[prm_id].str;
}

int *
prm_get_integer_list_value (PARAM_ID prm_id)
{
  return g_Prmdef[prm_id].ilist;
}

UINT64
prm_get_bigint_value (PARAM_ID prm_id)
{
  return g_Prmdef[prm_id].ull;
}

void
prm_set_integer_value (PARAM_ID prm_id, int value)
{
  g_Prmdef[prm_id].i = value;
}

void
prm_set_float_value (PARAM_ID prm_id, float value)
{
  g_Prmdef[prm_id].f = value;
}

void
prm_set_bool_value (PARAM_ID prm_id, bool value)
{
  g_Prmdef[prm_id].b = value;
}

void
prm_set_string_value (PARAM_ID prm_id, char *value)
{
  g_Prmdef[prm_id].str = value;
}

void
prm_set_integer_list_value (PARAM_ID prm_id, int *value)
{
  g_Prmdef[prm_id].ilist = value;
}

void
prm_set_bigint_value (PARAM_ID prm_id, UINT64 value)
{
  g_Prmdef[prm_id].ull = value;
}
