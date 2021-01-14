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
#ifndef CCI_APPLIER_H_
#define CCI_APPLIER_H_

#ident "$Id$"

#include "error_code.h"

#define CA_IS_SERVER_DOWN(e) \
  (((e) == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED) \
   || ((e) == ER_OBJ_NO_CONNECT) || ((e) == ER_NET_SERVER_CRASHED) \
   || ((e) == ER_BO_CONNECT_FAILED) || ((e) == ER_NET_CANT_CONNECT_SERVER))

#define CA_STOP_ON_ERROR(cci_err, server_err) \
  ((cci_err) != CCI_ER_NO_ERROR && \
  ((cci_err) != CCI_ER_DBMS || CA_IS_SERVER_DOWN (server_err)))
#define CA_RETRY_ON_ERROR(e) \
  (LA_RETRY_ON_ERROR (e) || (e) == ER_DB_NO_MODIFICATIONS)

#define ER_CA_NO_ERROR            0
#define ER_CA_FAILED              -1
#define ER_CA_FILE_IO             -2
#define ER_CA_FAILED_TO_ALLOC     -3
#define ER_CA_DISCREPANT_INFO     -4

#define CA_MARK_TRAN_START      "/* TRAN START */"
#define CA_MARK_TRAN_END        "/* TRAN END */"

#endif /* CCI_APPLIER_H_ */
