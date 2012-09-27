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

/* ====== define data types ====== */

/* Driver handle */
struct imp_drh_st {
	dbih_drc_t com;		/* MUST be first element in structure	*/
};


/* Define dbh implementor data structure */
struct imp_dbh_st {
	dbih_dbc_t com;		/* MUST be first element in structure	*/

        int     handle;
};


/* Statement structure */

typedef void *T_CCI_LOB;

typedef struct {
    T_CCI_LOB lob;
    T_CCI_U_TYPE type;
} T_CUBRID_LOB;

struct imp_sth_st {
	dbih_stc_t com;		/* MUST be first element in structure	*/

        int     handle;
        int     conn;

        int     col_count;
        int     affected_rows;
        T_CCI_CUBRID_STMT   sql_type;
        T_CCI_COL_INFO      *col_info;
        T_CUBRID_LOB        *lob;
};

/* ------ define functions and external variables ------ */

SV * _cubrid_primary_key (SV *dbh, char *table);
SV * _cubrid_foreign_key (SV *dbh, char *pk_table, char *fk_table);

/* These defines avoid name clashes for multiple statically linked DBD's */

#define dbd_init		cubrid_init
#define dbd_db_disconnect	cubrid_db_disconnect
#define dbd_db_login6		cubrid_db_login6
#define dbd_db_commit		cubrid_db_commit
#define dbd_db_rollback		cubrid_db_rollback
#define dbd_db_cancel		cubrid_db_cancel
#define dbd_db_destroy		cubrid_db_destroy
#define dbd_db_STORE_attrib	cubrid_db_STORE_attrib
#define dbd_db_FETCH_attrib	cubrid_db_FETCH_attrib
#define dbd_st_prepare		cubrid_st_prepare
#define dbd_st_rows		cubrid_st_rows
#define dbd_st_cancel		cubrid_st_cancel
#define dbd_st_execute		cubrid_st_execute
#define dbd_st_fetch		cubrid_st_fetch
#define dbd_st_finish		cubrid_st_finish
#define dbd_st_destroy		cubrid_st_destroy
#define dbd_st_blob_read	cubrid_st_blob_read
#define dbd_st_STORE_attrib	cubrid_st_STORE_attrib
#define dbd_st_FETCH_attrib	cubrid_st_FETCH_attrib
#define dbd_describe		cubrid_describe
#define dbd_bind_ph		cubrid_bind_ph
#define dbd_db_ping             cubrid_db_ping
#define dbd_db_last_insert_id   cubrid_db_last_insert_id
#define dbd_db_quote            cubrid_db_quote

int cubrid_st_lob_get (SV *sth, int col);
int cubrid_st_lob_export (SV *sth, int index, char *file);
int cubrid_st_lob_import (SV *sth, int index, char *filename, IV sql_type);
int cubrid_st_lob_close (SV *sth);

/* end */
