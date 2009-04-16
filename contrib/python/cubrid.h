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

#include "Python.h"
#if PY_MAJOR_VERSION >= 3
#include "intobject.h"
#endif
#include "structmember.h"
#include "cas_cci.h"

#if PY_MAJOR_VERSION >= 3
#define PyString_FromString PyBytes_FromString
#define PyString_AsString PyBytes_AsString
#define PyString_Check PyBytes_Check
#endif

#define MAX_STR_LEN	255

#define CUBRID_ER_INVALID_SQL_TYPE              -2002
#define CUBRID_ER_CANNOT_GET_COLUMN_INFO        -2003
#define CUBRID_ER_INIT_ARRAY_FAIL               -2004
#define CUBRID_ER_UNKNOWN_TYPE                  -2005
#define CUBRID_ER_INVALID_PARAM                 -2006
#define CUBRID_ER_INVALID_ARRAY_TYPE            -2007
#define CUBRID_ER_NOT_SUPPORTED_TYPE            -2008
#define CUBRID_ER_OPEN_FILE                     -2009
#define CUBRID_ER_CREATE_TEMP_FILE              -2010
#define CUBRID_ER_TRANSFER_FAIL                 -2011

extern PyTypeObject connection_object_type;
extern PyTypeObject cursor_object_type;

typedef struct _connection_object {
  PyObject_HEAD
  int                   handle;
  char                  host[MAX_STR_LEN];
  int                   port;
  char                  db[MAX_STR_LEN];
  char                  user[MAX_STR_LEN];
} Connection_Object;

typedef struct _cursor_object {
  PyObject_HEAD
  int                   connection;
  int                   handle;
  int                   affected_rows;
  int                   async_mode;
  int                   col_count;
  int                   rowCount;
  T_CCI_COL_INFO        *col_info;
  PyObject				*description;
  char                  *last_stmt;
} Cursor_Object;
