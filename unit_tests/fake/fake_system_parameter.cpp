/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
