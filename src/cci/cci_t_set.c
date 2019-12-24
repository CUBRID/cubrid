/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */


/*
 * cci_t_set.c -
 */

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(WINDOWS)
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#include "cci_common.h"
#include "cas_cci.h"
#include "cci_t_set.h"
#include "cci_query_execute.h"
#include "cci_net_buf.h"
#include "cci_util.h"

/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/

/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

/************************************************************************
 * INTERFACE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PRIVATE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF INTERFACE FUNCTIONS 				*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF PUBLIC FUNCTIONS	 				*
 ************************************************************************/

void
t_set_free (T_SET * set)
{
  if (set == NULL)
    return;

  FREE_MEM (set->element);
  FREE_MEM (set->data_buf);
  hm_conv_value_buf_clear (&(set->conv_value_buffer));
  FREE_MEM (set);
}

int
t_set_size (T_SET * set)
{
  return (set->num_element);
}

int
t_set_element_type (T_SET * set)
{
  return set->type;
}

int
t_set_get (T_SET * set, int index, T_CCI_A_TYPE a_type, void *value, int *indicator)
{
  char *ele_value_p;
  char u_type;
  int data_size;
  int err_code;

  if (index <= 0 || index > set->num_element)
    return CCI_ER_SET_INDEX;

  ele_value_p = (char *) set->element[index - 1];
  u_type = t_set_element_type (set);

  NET_STR_TO_INT (data_size, ele_value_p);
  if (data_size <= 0)
    {
      *indicator = -1;
      if (a_type == CCI_A_TYPE_STR)
	{
	  *((void **) value) = NULL;
	}
      return 0;
    }

  ele_value_p += 4;
  *indicator = 0;

  switch (a_type)
    {
    case CCI_A_TYPE_STR:
      err_code =
	qe_get_data_str (&(set->conv_value_buffer), (T_CCI_U_TYPE) u_type, ele_value_p, data_size, value, indicator);
      break;
    case CCI_A_TYPE_INT:
      err_code = qe_get_data_int ((T_CCI_U_TYPE) u_type, ele_value_p, value);
      break;
    case CCI_A_TYPE_BIGINT:
      err_code = qe_get_data_bigint ((T_CCI_U_TYPE) u_type, ele_value_p, value);
      break;
    case CCI_A_TYPE_FLOAT:
      err_code = qe_get_data_float ((T_CCI_U_TYPE) u_type, ele_value_p, value);
      break;
    case CCI_A_TYPE_DOUBLE:
      err_code = qe_get_data_double ((T_CCI_U_TYPE) u_type, ele_value_p, value);
      break;
    case CCI_A_TYPE_BIT:
      err_code = qe_get_data_bit ((T_CCI_U_TYPE) u_type, ele_value_p, data_size, value);
      break;
    case CCI_A_TYPE_DATE:
      err_code = qe_get_data_date ((T_CCI_U_TYPE) u_type, ele_value_p, value);
      break;
    case CCI_A_TYPE_UINT:
    case CCI_A_TYPE_UBIGINT:
    default:
      return CCI_ER_TYPE_CONVERSION;
    }

  return err_code;
}

int
t_set_alloc (T_SET ** out_set)
{
  T_SET *set;

  *out_set = NULL;
  set = (T_SET *) MALLOC (sizeof (T_SET));
  if (set == NULL)
    return CCI_ER_NO_MORE_MEMORY;

  memset (set, 0, sizeof (T_SET));
  *out_set = set;
  return 0;
}

int
t_set_make (T_SET * set, char ele_type, int size, void *value, int *indicator)
{
  int i;
  T_NET_BUF net_buf;
  char u_type;
  int err_code;
  int *ele_index = NULL;

  net_buf_init (&net_buf);

  ele_index = (int *) MALLOC (sizeof (int) * size);
  if (ele_index == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  if (ele_type == CCI_U_TYPE_SHORT)
    {
      ele_type = CCI_U_TYPE_INT;
    }

  for (i = 0; i < size; i++)
    {
      ele_index[i] = net_buf.data_size;

      u_type = ele_type;
      if (indicator[i])
	{
	  u_type = CCI_U_TYPE_NULL;
	}

      switch (u_type)
	{
	case CCI_U_TYPE_NULL:
	  {
	    ADD_ARG_BYTES (&net_buf, NULL, 0);
	  }
	  break;
	case CCI_U_TYPE_CHAR:
	case CCI_U_TYPE_STRING:
	case CCI_U_TYPE_NCHAR:
	case CCI_U_TYPE_VARNCHAR:
	case CCI_U_TYPE_NUMERIC:
	case CCI_U_TYPE_ENUM:
	case CCI_U_TYPE_JSON:
	  {
	    char *ele_value;
	    ele_value = ((char **) value)[i];
	    ADD_ARG_STR (&net_buf, ele_value, (int) strlen (ele_value) + 1, NULL);
	  }
	  break;
	case CCI_U_TYPE_BIT:
	case CCI_U_TYPE_VARBIT:
	  {
	    T_CCI_BIT ele_value;
	    ele_value = ((T_CCI_BIT *) value)[i];
	    ADD_ARG_BYTES (&net_buf, ele_value.buf, ele_value.size);
	  }
	  break;
	case CCI_U_TYPE_BIGINT:
	  {
	    INT64 ele_value;
	    ele_value = ((INT64 *) value)[i];
	    ADD_ARG_BIGINT (&net_buf, ele_value);
	  }
	  break;
	case CCI_U_TYPE_INT:
	case CCI_U_TYPE_SHORT:
	  {
	    int ele_value;
	    ele_value = ((int *) value)[i];
	    ADD_ARG_INT (&net_buf, ele_value);
	  }
	  break;
	case CCI_U_TYPE_FLOAT:
	  {
	    float ele_value;
	    ele_value = ((float *) value)[i];
	    ADD_ARG_FLOAT (&net_buf, ele_value);
	  }
	  break;
	case CCI_U_TYPE_MONETARY:
	case CCI_U_TYPE_DOUBLE:
	  {
	    double ele_value;
	    ele_value = ((double *) value)[i];
	    ADD_ARG_DOUBLE (&net_buf, ele_value);
	  }
	  break;
	case CCI_U_TYPE_DATE:
	case CCI_U_TYPE_TIME:
	case CCI_U_TYPE_TIMESTAMP:
	case CCI_U_TYPE_DATETIME:
	  {
	    T_CCI_DATE ele_value;
	    ele_value = ((T_CCI_DATE *) value)[i];
	    ADD_ARG_DATETIME (&net_buf, &ele_value);
	  }
	  break;
	case CCI_U_TYPE_TIMESTAMPTZ:
	case CCI_U_TYPE_TIMESTAMPLTZ:
	case CCI_U_TYPE_DATETIMETZ:
	case CCI_U_TYPE_DATETIMELTZ:
	  {
	    T_CCI_DATE_TZ ele_value;
	    ele_value = ((T_CCI_DATE_TZ *) value)[i];
	    ADD_ARG_DATETIMETZ (&net_buf, &ele_value);
	  }
	  break;
	case CCI_U_TYPE_OBJECT:
	  {
	    char *tmp_str;
	    T_OBJECT ele_value;

	    tmp_str = ((char **) value)[i];
	    err_code = ut_str_to_oid (tmp_str, &ele_value);
	    if (err_code < 0)
	      {
		goto set_make_error;
	      }
	    ADD_ARG_OBJECT (&net_buf, &ele_value);
	  }
	  break;
	case CCI_U_TYPE_USHORT:
	case CCI_U_TYPE_UINT:
	case CCI_U_TYPE_UBIGINT:
	case CCI_U_TYPE_BLOB:
	case CCI_U_TYPE_CLOB:
	default:
	  err_code = CCI_ER_TYPE_CONVERSION;
	  goto set_make_error;
	}
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      goto set_make_error;
    }

  set->element = (void **) MALLOC (sizeof (void *) * size);
  if (set->element == NULL)
    {
      err_code = CCI_ER_NO_MORE_MEMORY;
      goto set_make_error;
    }

  set->data_buf = net_buf.data;
  set->data_size = net_buf.data_size;
  for (i = 0; i < size; i++)
    {
      set->element[i] = (char *) (set->data_buf) + ele_index[i];
    }
  set->num_element = size;
  set->type = ele_type;

  FREE_MEM (ele_index);
  return 0;

set_make_error:
  net_buf_clear (&net_buf);
  FREE_MEM (ele_index);
  return err_code;
}

int
t_set_decode (T_SET * set)
{
  void **element;
  char *cur_p;
  int i;
  int remain_size;
  int ele_size;

  if (set->num_element < 0 || set->data_size < 0)
    return CCI_ER_COMMUNICATION;

  if (set->num_element == 0 || set->data_size == 0)
    return 0;

  element = (void **) MALLOC (sizeof (void *) * set->num_element);
  if (element == NULL)
    return CCI_ER_NO_MORE_MEMORY;
  memset (element, 0, sizeof (void *) * set->num_element);

  remain_size = set->data_size;
  cur_p = (char *) set->data_buf;

  for (i = 0; i < set->num_element; i++)
    {
      element[i] = cur_p;
      if (remain_size < 4)
	{
	  FREE_MEM (element);
	  return CCI_ER_COMMUNICATION;
	}
      NET_STR_TO_INT (ele_size, cur_p);
      cur_p += 4;
      remain_size -= 4;

      if (ele_size <= 0)
	continue;

      if (remain_size < ele_size)
	{
	  FREE_MEM (element);
	  return CCI_ER_COMMUNICATION;
	}
      cur_p += ele_size;
      remain_size -= ele_size;
    }

  set->element = element;
  return 0;
}

int
t_set_to_str (T_SET * set, T_VALUE_BUF * conv_val)
{
  T_NET_BUF net_buf;
  char *buf;
  int indicator;
  int err_code;
  int i;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, "{", 1);

  for (i = 1; i <= set->num_element; i++)
    {
      if (i != 1)
	{
	  net_buf_cp_str (&net_buf, ", ", 2);
	}

      buf = NULL;
      err_code = t_set_get (set, i, CCI_A_TYPE_STR, (void *) &buf, &indicator);
      if (err_code < 0)
	{
	  net_buf_clear (&net_buf);
	  return err_code;
	}

      if (indicator < 0 || buf == NULL)
	{
	  buf = (char *) "NULL";
	}

      net_buf_cp_str (&net_buf, buf, (int) strlen (buf));
    }

  net_buf_cp_str (&net_buf, "}", 2);

  hm_conv_value_buf_alloc (conv_val, net_buf.data_size);
  strcpy ((char *) conv_val->data, net_buf.data);

  net_buf_clear (&net_buf);

  return 0;
}

/************************************************************************
 * IMPLEMENTATION OF PRIVATE FUNCTIONS	 				*
 ************************************************************************/
