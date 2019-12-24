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
 * cci_map.h
 */

#ifndef CCI_MAP_H_
#define CCI_MAP_H_

#include "cas_cci.h"

/* one time connection_id */
extern T_CCI_ERROR_CODE map_open_otc (T_CCI_CONN connection_id, T_CCI_CONN * mapped_conn_id);
extern T_CCI_ERROR_CODE map_get_otc_value (T_CCI_CONN mapped_conn_id, T_CCI_CONN * connection_id, bool force);
extern T_CCI_ERROR_CODE map_close_otc (T_CCI_CONN mapped_conn_id);

/* one time statement_id */
extern T_CCI_ERROR_CODE map_open_ots (T_CCI_REQ statement_id, T_CCI_REQ * mapped_stmt_id);
extern T_CCI_ERROR_CODE map_get_ots_value (T_CCI_REQ mapped_stmt_id, T_CCI_REQ * statement_id, bool force);
extern T_CCI_ERROR_CODE map_close_ots (T_CCI_REQ mapped_stmt_id);

#endif /* CCI_MAP_H_ */
