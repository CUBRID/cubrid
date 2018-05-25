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
 * shard_key_func.c -
 *
 */

#ident "$Id$"


#include "shard_key_func.h"
#include "broker_shm.h"
#include "shard_metadata.h"
/* FOR DEBUG MSG -- remove this */
#include "broker_config.h"

#if defined(CUB_PROXY)
#include "shard_proxy_log.h"
#else /* CUB_PROXY */
#if defined(WINDOWS)
#define PROXY_LOG(level, fmt, ...)
#else /* WINDOWS */
#define PROXY_LOG(level, fmt, args...)
#endif /* !WINDOWS */
#endif /* !CUB_PROXY */

extern T_SHM_PROXY *shm_proxy_p;

int
register_fn_get_shard_key (void)
{
  int error;

  if (shm_proxy_p->shard_key_library_name[0] != '\0' && shm_proxy_p->shard_key_function_name[0] != '\0')
    {
      error = load_shard_key_function (shm_proxy_p->shard_key_library_name, shm_proxy_p->shard_key_function_name);
      if (error < 0)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to load " "shard hashing library. " "(library_name:[%s], function:[%s]).\n",
		     shm_proxy_p->shard_key_library_name, shm_proxy_p->shard_key_function_name);
	  close_shard_key_function ();
	  return -1;
	}

      PROXY_LOG (PROXY_LOG_MODE_NOTICE,
		 "Loading shard hashing " "library was completed. " "(library_name:[%s], function:[%s]).\n",
		 shm_proxy_p->shard_key_library_name, shm_proxy_p->shard_key_function_name);
      return 0;
    }

  fn_get_shard_key = fn_get_shard_key_default;
#if defined(SHARD_VERBOSE_DEBUG)
  printf ("FN_GET_SHARD_KEY REGISTER DEFAULT DONE\n");
#endif

  return 0;
}

int
fn_get_shard_key_default (const char *shard_key, T_SHARD_U_TYPE type, const void *value, int value_len)
{
  int modular_key;

  if (value == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid shard key value. " "Shard key value couldn't be NUll.");
      return ERROR_ON_ARGUMENT;
    }

  modular_key = shm_proxy_p->shard_key_modular;
  if (modular_key < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid modular key. " "Shard modular key value couldn't be negative integer. " "(modular_key:%d).",
		 modular_key);
      return ERROR_ON_MAKE_SHARD_KEY;
    }

  if (type == SHARD_U_TYPE_INT || type == SHARD_U_TYPE_UINT)
    {
      unsigned int ival;
      ival = (unsigned int) (*(unsigned int *) value);
      return ival % modular_key;
    }
  else if (type == SHARD_U_TYPE_SHORT || type == SHARD_U_TYPE_USHORT)
    {
      unsigned short sval;
      sval = (unsigned short) (*(unsigned short *) value);
      return sval % modular_key;
    }
  else if (type == SHARD_U_TYPE_BIGINT || type == SHARD_U_TYPE_UBIGINT)
    {
      UINT64 lval;
      lval = (UINT64) (*(UINT64 *) value);
      return lval % modular_key;
    }
  else
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unexpected shard key type. " "(type:%d).", type);
      return ERROR_ON_ARGUMENT;
    }
}

int
proxy_find_shard_id_by_hint_value (SP_VALUE * value_p, const char *key_column)
{
  int shard_key_id = -1;
  INT64 shard_key_val_int;
  char *shard_key_val_string;
  int shard_key_val_len;

  if (value_p->type == VT_INTEGER)
    {
      shard_key_val_int = value_p->integer;
      shard_key_id = (*fn_get_shard_key) (key_column, SHARD_U_TYPE_BIGINT, &shard_key_val_int, sizeof (INT64));
    }
  else if (value_p->type == VT_STRING)
    {
      shard_key_val_string = value_p->string.value;
      shard_key_val_len = value_p->string.length;
      shard_key_id = (*fn_get_shard_key) (key_column, SHARD_U_TYPE_STRING, shard_key_val_string, shard_key_val_len);
    }
  else
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid hint value type. (value_type:%d).", value_p->type);
    }
  return shard_key_id;
}
