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
 * cas_meta.c -
 */

#ident "$Id$"

#include <assert.h>

#include "porting.h"
#include "cas_protocol.h"

#define SET_BIT(C,B)	((C) |= (B))
#define CLEAR_BIT(C,B)	((C) &= ~(B))
#define IS_SET_BIT(C,B)	(((C) & (B)) == (B))

static char broker_info[BROKER_INFO_SIZE] = {
  CAS_DBMS_CUBRID,
  CAS_KEEP_CONNECTION_ON,
  CAS_STATEMENT_POOLING_ON,
  CCI_PCONNECT_ON,
  CAS_PROTO_PACK_CURRENT_NET_VER,
  (char) BROKER_RENEWED_ERROR_CODE | (char) BROKER_SUPPORT_HOLDABLE_RESULT,
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
cas_bi_set_oracle_compat_number_behavior (char oracle_compat_number_behavior)
{
  assert (oracle_compat_number_behavior == 0 || oracle_compat_number_behavior == 1);

  if (oracle_compat_number_behavior)
    {
      SET_BIT (broker_info[BROKER_INFO_SYSTEM_PARAM], MASK_ORACLE_COMPAT_NUMBER_BEHAVIOR);
    }
  else
    {
      CLEAR_BIT (broker_info[BROKER_INFO_SYSTEM_PARAM], MASK_ORACLE_COMPAT_NUMBER_BEHAVIOR);
    }
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
      SET_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG], BROKER_RENEWED_ERROR_CODE);
      break;
    case BI_FUNC_SUPPORT_HOLDABLE_RESULT:
      SET_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG], BROKER_SUPPORT_HOLDABLE_RESULT);
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
      CLEAR_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG], BROKER_RENEWED_ERROR_CODE);
      break;
    case BI_FUNC_SUPPORT_HOLDABLE_RESULT:
      CLEAR_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG], BROKER_SUPPORT_HOLDABLE_RESULT);
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
      return IS_SET_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG], BROKER_RENEWED_ERROR_CODE);
    case BI_FUNC_SUPPORT_HOLDABLE_RESULT:
      return IS_SET_BIT (broker_info[BROKER_INFO_FUNCTION_FLAG], BROKER_SUPPORT_HOLDABLE_RESULT);
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
  if (!IS_SET_BIT (driver_info[SRV_CON_MSG_IDX_PROTO_VERSION], CAS_PROTO_INDICATOR))
    {
      return false;
    }

  return IS_SET_BIT (driver_info[DRIVER_INFO_FUNCTION_FLAG], BROKER_RENEWED_ERROR_CODE);
}

void
cas_bi_make_broker_info (char *broker_info, char dbms_type, char statement_pooling, char cci_pconnect)
{
  broker_info[BROKER_INFO_DBMS_TYPE] = dbms_type;
  broker_info[BROKER_INFO_KEEP_CONNECTION] = CAS_KEEP_CONNECTION_ON;
  if (statement_pooling)
    {
      broker_info[BROKER_INFO_STATEMENT_POOLING] = CAS_STATEMENT_POOLING_ON;
    }
  else
    {
      broker_info[BROKER_INFO_STATEMENT_POOLING] = CAS_STATEMENT_POOLING_OFF;
    }
  broker_info[BROKER_INFO_CCI_PCONNECT] = (cci_pconnect ? CCI_PCONNECT_ON : CCI_PCONNECT_OFF);

  broker_info[BROKER_INFO_PROTO_VERSION] = CAS_PROTO_PACK_CURRENT_NET_VER;
  broker_info[BROKER_INFO_FUNCTION_FLAG] = (char) (BROKER_RENEWED_ERROR_CODE | BROKER_SUPPORT_HOLDABLE_RESULT);
  broker_info[BROKER_INFO_SYSTEM_PARAM] = 0;
  broker_info[BROKER_INFO_RESERVED3] = 0;
}
