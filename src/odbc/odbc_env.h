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

#ifndef	__ODBC_ENV_HEADER	/* to avoid multiple inclusion */
#define	__ODBC_ENV_HEADER

#include		"odbc_portable.h"
#include		"odbc_diag_record.h"

typedef struct st_odbc_env
{
  unsigned short handle_type;
  ODBC_DIAG *diag;
  struct st_odbc_env *next;
  void *conn;
  /*
     struct odbc_connection *conn;
   */

  char *program_name;

  /* ODBC environment attributes */
  unsigned long attr_odbc_version;
  unsigned long attr_output_nts;

  /*  (Not supported) Optional features */
  unsigned long attr_connection_pooling;
  unsigned long attr_cp_match;
} ODBC_ENV;

PUBLIC RETCODE odbc_alloc_env (ODBC_ENV ** envptr);
PUBLIC RETCODE odbc_free_env (ODBC_ENV * env);
PUBLIC RETCODE odbc_set_env_attr (ODBC_ENV * env,
				  long attribute,
				  void *valueptr, long stringlength);
PUBLIC odbc_get_env_attr (ODBC_ENV * env,
			  long attribute,
			  void *value_ptr,
			  long buffer_length, long *string_length_ptr);
PUBLIC RETCODE odbc_end_tran (short handle_type,
			      void *handle, short completion_type);

#endif /* ! __ODBC_ENV_HEADER */
