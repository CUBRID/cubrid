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

#ifndef	_CAS_ORACLE_H_
#define	_CAS_ORACLE_H_

#ident "$Id$"

#include "oci.h"
#include "cas_dbms_util.h"

#define DBMS_ORACLE       CCI_DBMS_CUBRID
#define ORA_SUCCESS(code) (code) == OCI_SUCCESS || (code) == OCI_SUCCESS_WITH_INFO

typedef struct oracle_info ORACLE_INFO;
struct oracle_info
{
  char name[SRV_CON_DBNAME_SIZE];
  char user[SRV_CON_DBUSER_SIZE];
  char pass[SRV_CON_DBPASSWD_SIZE];

  OCIEnv *env;
  OCIError *err;
  OCISvcCtx *svc;
};

typedef struct oracle_value ORACLE_VALUE;
struct oracle_value
{
  void *buffer;
  int size;
  OCITypeCode code;
};

extern int cas_oracle_query_cancel (void);
extern int cas_oracle_stmt_close (void *session);
#endif /* _CAS_ORACLE_H_ */
