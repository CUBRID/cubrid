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

/*
 * cas_meta.c -
 */

#ident "$Id$"

#include <assert.h>

#include "porting.h"
#include "cas_protocol.h"

#define SET_BIT(C,B)	(C) |= (B)
#define CLEAR_BIT(C,B)	(C) &= ~(B)
#define IS_SET_BIT(C,B)	((C) & (B)) == (B)

static char broker_info[BROKER_INFO_SIZE] = {
  CAS_DBMS_CUBRID,
  CAS_KEEP_CONNECTION_ON,
  CAS_STATEMENT_POOLING_ON,
  CCI_PCONNECT_ON,
  CAS_PROTO_PACK_CURRENT_NET_VER,
  BROKER_RENEWED_ERROR_CODE | BROKER_SUPPORT_HOLDABLE_RESULT,
  0,
  0
};

typedef enum
{
  BI_FUNC_ERROR_CODE,
  BI_FUNC_SUPPORT_HOLDABLE_RESULT
} BI_FUNCTION_CODE;

const char *
cas_bi_get_broker_info (void)
{
  return broker_info;
}

void
cas_bi_set_dbms_type (const char dbms_type)
{
  broker_info[BROKER_INFO_DBMS_TYPE] = dbms_type;
}

char
cas_bi_get_dbms_type (void)
{
  return broker_info[BROKER_INFO_DBMS_TYPE];
}

void
cas_bi_set_keep_connection (const char keep_connection)
{
  broker_info[BROKER_INFO_KEEP_CONNECTION] = keep_connection;
}

char
cas_bi_get_keep_connection (void)
{
  return broker_info[BROKER_INFO_KEEP_CONNECTION];
}

void
cas_bi_set_statement_pooling (const char statement_pooling)
{
  assert (statement_pooling == 0 || statement_pooling == 1);

  if (statement_pooling)
    {
      broker_info[BROKER_INFO_STATEMENT_POOLING] = CAS_STATEMENT_POOLING_ON;
    }
  else
    {
      broker_info[BROKER_INFO_STATEMENT_POOLING] = CAS_STATEMENT_POOLING_OFF;
    }
}

char
cas_bi_get_statement_pooling (void)
{
  return broker_info[BROKER_INFO_STATEMENT_POOLING];
}

void
cas_bi_set_cci_pconnect (const char cci_pconnect)
{
  assert (cci_pconnect == 0 || cci_pconnect == 1);

  if (cci_pconnect)
    {
      broker_info[BROKER_INFO_CCI_PCONNECT] = CCI_PCONNECT_ON;
    }
  else
    {
      broker_info[BROKER_INFO_CCI_PCONNECT] = CCI_PCONNECT_OFF;
    }
}

char
cas_bi_get_cci_pconnect (void)
{
  return broker_info[BROKER_INFO_CCI_PCONNECT];
}

void
cas_bi_set_protocol_version (const char protocol_version)
{
  broker_info[BROKER_INFO_PROTO_VERSION] = protocol_version;
}

char
cas_bi_get_protocol_version (void)
{
  return broker_info[BROKER_INFO_PROTO_VERSION];
}

static void
cas_bi_set_function_enable (BI_FUNCTION_CODE function_code)
{
  switch (function_code)
    {
    case BI_FUNC_ERROR_CODE:
      SET_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG],
	       BROKER_RENEWED_ERROR_CODE);
      break;
    case BI_FUNC_SUPPORT_HOLDABLE_RESULT:
      SET_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG],
	       BROKER_SUPPORT_HOLDABLE_RESULT);
      break;
    default:
      assert (false);
      break;
    }
}

static void
cas_bi_set_function_disable (BI_FUNCTION_CODE function_code)
{
  switch (function_code)
    {
    case BI_FUNC_ERROR_CODE:
      CLEAR_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG],
		 BROKER_RENEWED_ERROR_CODE);
      break;
    case BI_FUNC_SUPPORT_HOLDABLE_RESULT:
      CLEAR_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG],
		 BROKER_SUPPORT_HOLDABLE_RESULT);
      break;
    default:
      assert (false);
      break;
    }
}

static bool
cas_bi_is_enabled_function (BI_FUNCTION_CODE function_code)
{
  switch (function_code)
    {
    case BI_FUNC_ERROR_CODE:
      return IS_SET_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG],
			 BROKER_RENEWED_ERROR_CODE);
    case BI_FUNC_SUPPORT_HOLDABLE_RESULT:
      return IS_SET_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG],
			 BROKER_SUPPORT_HOLDABLE_RESULT);
    default:
      return 0;
    }
}

void
cas_bi_set_renewed_error_code (const bool renewed_error_code)
{
  if (renewed_error_code)
    {
      cas_bi_set_function_enable (BI_FUNC_ERROR_CODE);
    }
  else
    {
      cas_bi_set_function_disable (BI_FUNC_ERROR_CODE);
    }
}

bool
cas_bi_get_renewed_error_code (void)
{
  return cas_bi_is_enabled_function (BI_FUNC_ERROR_CODE);
}

bool
cas_di_understand_renewed_error_code (const char *driver_info)
{
  if (!IS_SET_BIT (driver_info[SRV_CON_MSG_IDX_PROTO_VERSION],
		   CAS_PROTO_INDICATOR))
    {
      return false;
    }

  return IS_SET_BIT (driver_info[DRIVER_INFO_FUNCTION_FLAG],
		     BROKER_RENEWED_ERROR_CODE);
}

void
cas_bi_make_broker_info (char *broker_info, char statement_pooling,
			 char cci_pconnect)
{
  broker_info[BROKER_INFO_DBMS_TYPE] = CAS_DBMS_CUBRID;
  broker_info[BROKER_INFO_KEEP_CONNECTION] = CAS_KEEP_CONNECTION_ON;
  if (statement_pooling)
    {
      broker_info[BROKER_INFO_STATEMENT_POOLING] = CAS_STATEMENT_POOLING_ON;
    }
  else
    {
      broker_info[BROKER_INFO_STATEMENT_POOLING] = CAS_STATEMENT_POOLING_OFF;
    }
  broker_info[BROKER_INFO_CCI_PCONNECT] =
    (cci_pconnect ? CCI_PCONNECT_ON : CCI_PCONNECT_OFF);

  broker_info[BROKER_INFO_PROTO_VERSION] = CAS_PROTO_PACK_CURRENT_NET_VER;
  broker_info[BROKER_INFO_FUNCTION_FLAG]
    = BROKER_RENEWED_ERROR_CODE | BROKER_SUPPORT_HOLDABLE_RESULT;
  broker_info[BROKER_INFO_RESERVED2] = 0;
  broker_info[BROKER_INFO_RESERVED3] = 0;
}
