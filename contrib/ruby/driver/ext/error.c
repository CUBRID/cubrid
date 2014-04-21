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

#include "cubrid.h"

static struct _error_message {
  int   err;
  char* msg;
} cubrid_err_msgs[] = {
{-1,      "CUBRID database error"},
{-2,      "Invalid connection handle"},
{-3,      "Memory allocation error"},
{-4,      "Communication error"},
{-5,      "No more data"},
{-6,      "Unknown transaction type"},
{-7,      "Invalid string parameter"},
{-8,      "Type conversion error"},
{-9,      "Parameter binding error"},
{-10,     "Invalid type"},
{-11,     "Parameter binding error"},
{-12,     "Invalid database parameter name"},
{-13,     "Invalid column index"},
{-14,     "Invalid schema type"},
{-15,     "File open error"},
{-16,     "Connection error"},
{-17,     "Connection handle creation error"},
{-18,     "Invalid request handle"},
{-19,     "Invalid cursor position"},
{-20,     "Object is not valid"},
{-21,     "CAS error"},
{-22,     "Unknown host name"},
{-99,     "Not implemented"},
{-1000,   "Database connection error"},
{-1002,   "Memory allocation error"},
{-1003,   "Communication error"},
{-1004,   "Invalid argument"},
{-1005,   "Unknown transaction type"},
{-1007,   "Parameter binding error"},
{-1008,   "Parameter binding error"},
{-1009,   "Cannot make DB_VALUE"},
{-1010,   "Type conversion error"},
{-1011,   "Invalid database parameter name"},
{-1012,   "No more data"},
{-1013,   "Object is not valid"},
{-1014,   "File open error"},
{-1015,   "Invalid schema type"},
{-1016,   "Version mismatch"},
{-1017,   "Cannot process the request. Try again later."},
{-1018,   "Authorization error"},
{-1020,   "The attribute domain must be the set type."},
{-1021,   "The domain of a set must be the same data type."},
{-2001,   "Memory allocation error"},
{-2002,   "Invalid API call"},
{-2003,   "Cannot get column info"},
{-2004,   "Array initializing error"},
{-2005,   "Unknown column type"},
{-2006,   "Invalid parameter"},
{-2007,   "Invalid array type"},
{-2008,   "Invalid type"},
{-2009,   "File open error"},
{-2010,   "Temporary file open error"},
{-2011,   "Glo transfering error"},
{0,       "Unknown Error"}
};

static char * 
get_error_msg(int err_code)
{
  int i;

  for(i = 0; ; i++) {
    if (!cubrid_err_msgs[i].err || cubrid_err_msgs[i].err == err_code) {
      return cubrid_err_msgs[i].msg;
    }
  }
  return NULL;
}

void
cubrid_handle_error(int e, T_CCI_ERROR *error)
{
  int       err_code;
  char      msg[1024];
  char      *err_msg = NULL, *facility_msg;

  if (e == CCI_ER_DBMS) {
    facility_msg = "DBMS";
    if (error) {
      err_code = error->err_code;
      err_msg = error->err_msg;
    } else {
      err_code = 0;
      err_msg = "Unknown DBMS Error";
    }
  } else {
    err_msg = get_error_msg(e);
    err_code = e;

    if (e > -1000) {
      facility_msg = "CCI";
    } else if (e > -2000) {
      facility_msg = "CAS";
    } else if (e > -3000) {
      facility_msg = "CLIENT";
    } else {
      facility_msg = "UNKNOWN";
    }
  }

  if (!err_msg) {
    err_msg = "Unknown Error";
  }

  sprintf(msg, "ERROR: %s, %d, %s", facility_msg, err_code, err_msg);
  rb_raise(rb_eStandardError, msg);
}

