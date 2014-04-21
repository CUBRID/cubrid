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
 * cci_t_lob.c -
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

#include "cci_net_buf.h"
#include "cci_t_lob.h"

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

INT64
t_lob_get_size (char *pack_bytes)
{
  INT64 size;
  INT64 temp;

  memcpy (&temp, pack_bytes + 4, sizeof(INT64));
  size = ntohi64 (temp);
  return size;
}

void
t_lob_set_size (T_LOB * lob, INT64 new_size)
{
  new_size = htoni64 (new_size);
  memcpy (lob->handle + 4, &new_size, sizeof(INT64));
}

/************************************************************************
 * IMPLEMENTATION OF PRIVATE FUNCTIONS	 				*
 ************************************************************************/
