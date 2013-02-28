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

/* ====== Include CUBRID Header Files ====== */

#include "cas_cci.h"

/* ------ end of CUBRID include files ------ */

//#define NEED_DBIXS_VERSION 93

//#define PERL_NO_GET_CONTEXT

#include <DBIXS.h>		/* installed by the DBI module	*/

#include "dbdimp.h"

#include <dbd_xsh.h>		/* installed by the DBI module	*/

DBISTATE_DECLARE;

/* These prototypes are for dbdimp.c funcs used in the XS file          */
/* These names are #defined to driver specific names in dbdimp.h        */


/* CUBRID types */

#define CUBRID_ER_START                     -30000
#define CUBRID_ER_CANNOT_GET_COLUMN_INFO    -30001
#define CUBRID_ER_CANNOT_FETCH_DATA         -30002
#define CUBRID_ER_WRITE_FILE                -30003
#define CUBRID_ER_READ_FILE                 -30004
#define CUBRID_ER_NOT_LOB_TYPE              -30005
#define CUBRID_ER_INVALID_PARAM             -30006
#define CUBRID_ER_ROW_INDEX_EXCEEDED        -30007
#define CUBRID_ER_EXPORT_NULL_LOB_INVALID   -30008 
#define CUBRID_ER_END                       -31000

/* end of cubrid.h */

