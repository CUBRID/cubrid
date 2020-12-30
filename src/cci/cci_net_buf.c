/*
 * Copyright (C) 2008 Search Solution Corporation. 
 * Copyright (c) 2016 CUBRID Corporation.
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
 * cci_net_buf.c -
 */

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/
#include "config.h"

#include <string.h>
#include <stdlib.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#endif

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#include "cci_common.h"
#include "cas_cci.h"
#include "cci_net_buf.h"

/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/

/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static int net_buf_realloc (T_NET_BUF * net_buf, int size);

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
cnet_buf_init (T_NET_BUF * net_buf)
{
  net_buf->alloc_size = 0;
  net_buf->data_size = 0;
  net_buf->data = NULL;
  net_buf->err_code = 0;
}

void
cnet_buf_clear (T_NET_BUF * net_buf)
{
  FREE_MEM (net_buf->data);
  net_buf_init (net_buf);
}

int
cnet_buf_cp_str (T_NET_BUF * net_buf, const char *buf, int size)
{
  if (size <= 0)
    return 0;

  if (net_buf_realloc (net_buf, size) < 0)
    return CCI_ER_NO_MORE_MEMORY;

  memcpy (net_buf->data + net_buf->data_size, buf, size);
  net_buf->data_size += size;
  return 0;
}

int
cnet_buf_cp_bigint (T_NET_BUF * net_buf, INT64 value)
{
  int size = NET_SIZE_BIGINT;

  if (net_buf_realloc (net_buf, size) < 0)
    return CCI_ER_NO_MORE_MEMORY;

  value = htoni64 (value);
  memcpy (net_buf->data + net_buf->data_size, &value, size);
  net_buf->data_size += size;

  return 0;
}

int
cnet_buf_cp_byte (T_NET_BUF * net_buf, char value)
{
  int size = NET_SIZE_BYTE;

  if (net_buf_realloc (net_buf, size) < 0)
    return CCI_ER_NO_MORE_MEMORY;

  *(net_buf->data + net_buf->data_size) = value;	/* do not call memcpy(); simply assign */
  net_buf->data_size += size;

  return 0;
}

int
cnet_buf_cp_int (T_NET_BUF * net_buf, int value)
{
  int size = NET_SIZE_INT;

  if (net_buf_realloc (net_buf, size) < 0)
    return CCI_ER_NO_MORE_MEMORY;

  value = htonl (value);
  memcpy (net_buf->data + net_buf->data_size, &value, size);
  net_buf->data_size += size;

  return 0;
}

int
cnet_buf_cp_float (T_NET_BUF * net_buf, float value)
{
  int size = NET_SIZE_FLOAT;

  if (net_buf_realloc (net_buf, size) < 0)
    return CCI_ER_NO_MORE_MEMORY;
  value = htonf (value);
  memcpy (net_buf->data + net_buf->data_size, &value, size);
  net_buf->data_size += size;

  return 0;
}

int
cnet_buf_cp_double (T_NET_BUF * net_buf, double value)
{
  int size = NET_SIZE_DOUBLE;

  if (net_buf_realloc (net_buf, size) < 0)
    return CCI_ER_NO_MORE_MEMORY;
  value = htond (value);
  memcpy (net_buf->data + net_buf->data_size, &value, size);
  net_buf->data_size += size;

  return 0;
}

int
cnet_buf_cp_short (T_NET_BUF * net_buf, short value)
{
  int size = NET_SIZE_SHORT;

  if (net_buf_realloc (net_buf, size) < 0)
    return CCI_ER_NO_MORE_MEMORY;
  value = htons (value);
  memcpy (net_buf->data + net_buf->data_size, &value, size);
  net_buf->data_size += size;

  return 0;
}

#ifndef BYTE_ORDER_BIG_ENDIAN
INT64
cnet_buf_htoni64 (INT64 from)
{
  INT64 to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[7];
  q[1] = p[6];
  q[2] = p[5];
  q[3] = p[4];
  q[4] = p[3];
  q[5] = p[2];
  q[6] = p[1];
  q[7] = p[0];

  return to;
}

float
cnet_buf_htonf (float from)
{
  float to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[3];
  q[1] = p[2];
  q[2] = p[1];
  q[3] = p[0];

  return to;
}

double
cnet_buf_htond (double from)
{
  double to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[7];
  q[1] = p[6];
  q[2] = p[5];
  q[3] = p[4];
  q[4] = p[3];
  q[5] = p[2];
  q[6] = p[1];
  q[7] = p[0];

  return to;
}
#endif

/************************************************************************
 * IMPLEMENTATION OF PRIVATE FUNCTIONS	 				*
 ************************************************************************/

static int
net_buf_realloc (T_NET_BUF * net_buf, int size)
{
  int new_alloc_size;

  if (size + net_buf->data_size > net_buf->alloc_size)
    {
      new_alloc_size = net_buf->alloc_size + 1024;
      if (size + net_buf->data_size > new_alloc_size)
	{
	  new_alloc_size = size + net_buf->data_size;
	}
      net_buf->data = (char *) REALLOC (net_buf->data, new_alloc_size);
      if (net_buf->data == NULL)
	{
	  net_buf->alloc_size = 0;
	  net_buf->err_code = CCI_ER_NO_MORE_MEMORY;
	  return -1;
	}

      net_buf->alloc_size = new_alloc_size;
    }

  return 0;
}
