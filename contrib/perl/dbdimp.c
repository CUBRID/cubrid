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
#include <fcntl.h>

#ifdef WIN32
#define  open(file, flag, mode) PerlLIO_open3(file, flag, mode)
#endif

#define CUBRID_ER_MSG_LEN 1024
#define CUBRID_BUFFER_LEN 4096

static struct _error_message {
    int err_code;
    char *err_msg;
} cubrid_err_msgs[] = {
    {CUBRID_ER_CANNOT_GET_COLUMN_INFO, "Cannot get column info"},
    {CUBRID_ER_CANNOT_FETCH_DATA, "Cannot fetch data"},
    {CUBRID_ER_WRITE_FILE, "Cannot write file"},
    {CUBRID_ER_READ_FILE, "Cannot read file"},
    {CUBRID_ER_NOT_LOB_TYPE, "Not a lob type, can only support SQL_BLOB or SQL_CLOB"},
    {0, ""}
};


/***************************************************************************
 * Private function prototypes
 **************************************************************************/

static int _dbd_db_end_tran (SV *dbh, imp_dbh_t *imp_dbh, int type);

static int _cubrid_lob_bind (SV *sv,
                             int index,
                             IV sql_type,
                             char *buf,
                             T_CCI_ERROR *error);
static int _cubrid_lob_new (int conn, 
                            T_CCI_LOB *lob, 
                            T_CCI_U_TYPE type, 
                            T_CCI_ERROR *error);
static long long _cubrid_lob_size( T_CCI_LOB lob, T_CCI_U_TYPE type );
static int _cubrid_lob_write (int conn, 
                              T_CCI_LOB lob, 
                              T_CCI_U_TYPE type, 
                              long long start_pos, 
                              int length, 
                              const char *buf, 
                              T_CCI_ERROR *error);
static int _cubrid_lob_read (int conn, 
                             T_CCI_LOB lob, 
                             T_CCI_U_TYPE type, 
                             long long start_pos, 
                             int length, 
                             char *buf, 
                             T_CCI_ERROR *error);
static int _cubrid_lob_free (T_CCI_LOB lob, T_CCI_U_TYPE type);

static int _cubrid_fetch_schema (AV *rows_av, 
                                 int req_handle, 
                                 int col_count, 
                                 T_CCI_COL_INFO *col_info, 
                                 T_CCI_ERROR *error);
static int _cubrid_fetch_row (AV *av, 
                              int req_handle, 
                              int col_count, 
                              T_CCI_COL_INFO *col_info, 
                              T_CCI_ERROR *error);

/***************************************************************************
 * 
 * Name:    dbd_init
 * 
 * Purpose: Called when the driver is installed by DBI
 *
 * Input:   dbistate - pointer to the DBIS variable, used for some
 *                     DBI internal things
 *
 * Returns: Nothing
 *
 **************************************************************************/

void
dbd_init( dbistate_t *dbistate )
{
    DBIS = dbistate;
    cci_init();
}

/***************************************************************************
 * 
 * Name:    dbd_discon_all
 * 
 * Purpose: Disconnect all database handles at shutdown time
 *
 * Input:   dbh - database handle being disconnecte
 *          imp_dbh - drivers private database handle data
 * 
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int
dbd_discon_all( SV *drh, imp_drh_t *imp_drh )
{
    cci_end ();

    if (!PL_dirty && !SvTRUE(perl_get_sv("DBI::PERL_ENDING",0))) {
        sv_setiv(DBIc_ERR(imp_drh), (IV)1);
        sv_setpv(DBIc_ERRSTR(imp_drh), (char*)"disconnect_all not implemented");
        return FALSE;
    }

    return FALSE;
}


/***************************************************************************
 * 
 * Name:    handle_error
 * 
 * Purpose: Called to associate an error code and an error message 
 *          to some handle
 *
 * Input:   h - the handle in error condition
 *          e - the error code
 *          error - the error message
 *
 * Returns: Nothing
 *
 **************************************************************************/

static int
get_error_msg( int err_code, char *err_msg )
{
    int i;

    for (i = 0; ; i++) {
        if (!cubrid_err_msgs[i].err_code)
            break;
        if (cubrid_err_msgs[i].err_code == err_code) {
            snprintf (err_msg, CUBRID_ER_MSG_LEN, 
                        "ERROR: CLIENT, %d, %s", 
                        err_code, cubrid_err_msgs[i].err_msg);
            return 0;
        }
    }
    return -1;
}

static void
handle_error( SV* h, int e, T_CCI_ERROR *error )
{
    D_imp_xxh (h);
    char msg[CUBRID_ER_MSG_LEN] = {'\0'};
    SV *errstr;

    if (DBIc_TRACE_LEVEL (imp_xxh) >= 2)
        PerlIO_printf (DBILOGFP, "\t\t--> handle_error\n");

    errstr = DBIc_ERRSTR (imp_xxh);
    sv_setiv (DBIc_ERR (imp_xxh), (IV)e);

    if (e < -2000) {
        get_error_msg (e, msg);
    } else if (cci_get_error_msg (e, error, msg, CUBRID_ER_MSG_LEN) < 0) {
        snprintf (msg, CUBRID_ER_MSG_LEN, "Unknown Error");
    }     

    sv_setpv (errstr, msg);

    if (DBIc_TRACE_LEVEL (imp_xxh) >= 2)
        PerlIO_printf (DBIc_LOGPIO (imp_xxh), "%s\n", msg);

    if (DBIc_TRACE_LEVEL (imp_xxh) >= 2)
        PerlIO_printf (DBILOGFP, "\t\t<-- handle_error\n");

    return;
}

/***************************************************************************
 * 
 * Name:    dbd_db_login6
 * 
 * Purpose: Called for connecting to a database and logging in.
 *
 * Input:   dbh - database handle being initialized
 *          imp_dbh - drivers private database handle data
 *          dbname - the database we want to log into; may be like
 *                   "dbname:host" or "dbname:host:port"
 *          user - user name to connect as
 *          password - passwort to connect with
 *
 * Returns: Nothing
 *
 **************************************************************************/

int
dbd_db_login6( SV *dbh, imp_dbh_t *imp_dbh,
        char *dbname, char *uid, char *pwd, SV *attr )
{
    int  con, res;
    T_CCI_ERROR error;

    if ((con = cci_connect_with_url (dbname, uid, pwd)) < 0) {
        handle_error (dbh, con, NULL);
        return FALSE;
    }

    imp_dbh->handle = con;

    if ((res = cci_end_tran (con, CCI_TRAN_COMMIT, &error)) < 0) {
        handle_error (dbh, res, &error);
        return FALSE;
    }

    DBIc_IMPSET_on(imp_dbh);
    DBIc_ACTIVE_on(imp_dbh);

    return TRUE;
}

/***************************************************************************
 *
 * Name:    dbd_db_commit
 *          dbd_db_rollback
 *
 * Purpose: Commit/Rollback any pending transaction to the database.
 *
 * Input:   dbh - database handle being commit/rollback
 *          imp_dbh - drivers private database handle data
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int
dbd_db_commit( SV *dbh, imp_dbh_t *imp_dbh )
{
    return _dbd_db_end_tran (dbh, imp_dbh, CCI_TRAN_COMMIT);
}

int
dbd_db_rollback( SV *dbh, imp_dbh_t *imp_dbh )
{
    return _dbd_db_end_tran (dbh, imp_dbh, CCI_TRAN_ROLLBACK);
}

static int
_dbd_db_end_tran( SV *dbh, imp_dbh_t *imp_dbh, int type )
{
    int res;
    T_CCI_ERROR error;

    if ((res = cci_end_tran (imp_dbh->handle, type, &error)) < 0) {
        handle_error (dbh, res, &error);
        return FALSE;
    }
    return TRUE;
}

/***************************************************************************
 *
 * Name:    dbd_db_destroy
 *
 * Purpose: Our part of the dbh destructor
 *
 * Input:   dbh - database handle being disconnected
 *          imp_dbh - drivers private database handle data
 *
 * Returns: Nothing
 *
 **************************************************************************/

void
dbd_db_destroy( SV *dbh, imp_dbh_t *imp_dbh )
{
    if (DBIc_ACTIVE(imp_dbh)) {
        (void)dbd_db_disconnect(dbh, imp_dbh);
    }

    DBIc_IMPSET_off(imp_dbh);
}

/***************************************************************************
 *
 * Name:    dbd_db_disconnect
 *
 * Purpose: Disconnect a database handle from its database
 *
 * Input:   dbh - database handle being disconnected
 *          imp_dbh - drivers private database handle data
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int
dbd_db_disconnect( SV *dbh, imp_dbh_t *imp_dbh )
{
    int res;
    T_CCI_ERROR error;

    DBIc_ACTIVE_off(imp_dbh);

    if ((res = cci_disconnect (imp_dbh->handle, &error)) < 0) {
        handle_error (dbh, res, &error);
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************
 *
 * Name:    dbd_db_STORE_attrib
 *
 * Purpose: Function for storing dbh attributes
 *
 * Input:   dbh - database handle being disconnected
 *          imp_dbh - drivers private database handle data
 *          keysv - the attribute name
 *          valuesv - the attribute value
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int
dbd_db_STORE_attrib( SV *dbh, imp_dbh_t *imp_dbh, SV *keysv, SV *valuesv )
{
    STRLEN kl;
    char *key = SvPV (keysv,kl);
    int on = SvTRUE (valuesv);

    switch (kl) {
    case 10:
        if (strEQ("AutoCommit", key)) {
            if (on) {
                cci_set_autocommit (imp_dbh->handle, CCI_AUTOCOMMIT_TRUE);
            }
            else {
                cci_set_autocommit (imp_dbh->handle, CCI_AUTOCOMMIT_FALSE);
            }

            DBIc_set (imp_dbh, DBIcf_AutoCommit, on);
        }
        break;
    }

    return TRUE;
}

/***************************************************************************
 *
 * Name:    dbd_db_FETCH_attrib
 *
 * Purpose: Function for fetching dbh attributes
 *
 * Input:   dbh - database handle being disconnected
 *          imp_dbh - drivers private database handle data
 *          keysv - the attribute name
 *          valuesv - the attribute value
 *
 * Returns: An SV*, if sucessfull; NULL otherwise
 *
 **************************************************************************/

SV * 
dbd_db_FETCH_attrib(SV *dbh, imp_dbh_t *imp_dbh, SV *keysv)
{
    STRLEN kl;
    char *key = SvPV (keysv, kl);
    SV *retsv = Nullsv;
    
    switch (kl) {
    case 10:
        if (strEQ("AutoCommit", key)) {
            retsv = boolSV(DBIc_has(imp_dbh,DBIcf_AutoCommit));
        }
        break;
    }
    return sv_2mortal(retsv);
}

/**************************************************************************
 *
 * Name:    dbd_db_last_insert_id
 *
 * Purpose: Returns a value identifying the row just inserted 
 *
 * Input:   dbh - database handle
 *          imp_dbh - drivers private database handle data
 *          catalog - NULL
 *          schema  - NULL
 *          table   - NULL
 *          field   - NULL
 *          attr    - NULL
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

SV *
dbd_db_last_insert_id( SV *dbh, imp_dbh_t *imp_dbh,
        SV *catalog, SV *schema, SV *table, SV *field, SV *attr )
{
    SV *sv;
    char *name = NULL;
    int res;
    T_CCI_ERROR error;

    if ((res = cci_last_insert_id (imp_dbh->handle, &name, &error))) {
        handle_error (dbh, res, &error);
        return Nullsv;
    }

    if (!name) {
        return Nullsv;
    } else {
        sv = newSVpvn (name, strlen(name));
        free (name);
    }

    return sv_2mortal (sv);
}

/**************************************************************************
 *
 * Name:    dbd_db_ping
 *
 * Purpose: Check whether the database server is still running and the 
 *          connection to it is still working.
 *
 * Input:   Nothing
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int
dbd_db_ping( SV *dbh )
{
    int res;
    T_CCI_ERROR error;
    char *query = "SELECT 1+1 from db_root";
    int req_handle = 0, result = 0, ind = 0;

    D_imp_dbh (dbh);

    if ((res = cci_prepare (imp_dbh->handle, query, 0, &error)) < 0) {
        handle_error (dbh, res, &error);
        return FALSE;
    }

    req_handle = res;

    if ((res = cci_execute (req_handle, 0, 0, &error)) < 0) {
        handle_error (dbh, res, &error);
        return FALSE;
    }

    while (1) {
        res = cci_cursor (req_handle, 1, CCI_CURSOR_CURRENT, &error);
        if (res == CCI_ER_NO_MORE_DATA) {
            break;
        }
        if (res < 0) {
            handle_error (dbh, res, &error);
            return FALSE;
        }

        if ((res = cci_fetch (req_handle, &error)) < 0) {
            handle_error (dbh, res, &error);
            return FALSE;
        }

        if ((res = cci_get_data (req_handle, 1, CCI_A_TYPE_INT, &result, &ind)) < 0) {
            handle_error (dbh, res, &error);
            return FALSE;
        }

        if (result == 2) {
            cci_close_req_handle (req_handle);
            return TRUE;
        }
    }

    cci_close_req_handle (req_handle);
    return FALSE;
}

/***************************************************************************
 *
 * Name:    dbd_st_prepare
 *
 * Purpose: Called for preparing an SQL statement; our part of the
 *          statement handle constructor
 *
 * Input:   sth - statement handle being initialized
 *          imp_sth - drivers private statement handle data
 *          statement - pointer to string with SQL statement
 *          attribs - statement attributes, currently not in use
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int
dbd_st_prepare( SV *sth, imp_sth_t *imp_sth, char *statement, SV *attribs )
{
    int res;
    T_CCI_ERROR error;

    D_imp_dbh_from_sth;

    if ('\0' == *statement)
        croak ("Cannot preapre empty statement");

    imp_sth->conn = imp_dbh->handle;

    imp_sth->col_count = -1;
    imp_sth->sql_type = 0;
    imp_sth->affected_rows = -1;
    imp_sth->lob = NULL;

    if ((res = cci_prepare (imp_sth->conn, statement, 0, &error)) < 0) {
        handle_error (sth, res, &error);
        return FALSE;
    }

    imp_sth->handle = res;

    DBIc_NUM_PARAMS(imp_sth) = cci_get_bind_num (res);

    DBIc_IMPSET_on(imp_sth);

    return TRUE;
}

/***************************************************************************
 *
 * Name:    dbd_st_execute
 *
 * Purpose: Called for execute the prepared SQL statement; our part of
 *          the statement handle constructor
 *
 * Input:   sth - statement handle
 *          imp_sth - drivers private statement handle data
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int
dbd_st_execute( SV *sth, imp_sth_t *imp_sth )
{
    int res, option = 0, max_col_size = 0;
    T_CCI_ERROR error;
    T_CCI_COL_INFO *col_info;
    T_CCI_SQLX_CMD sql_type;
    int col_count;

    if ((res = cci_execute (imp_sth->handle, option, 
                    max_col_size, &error)) < 0) {
        handle_error (sth, res, &error);
        return -2;
    }

    col_info = cci_get_result_info (imp_sth->handle, &sql_type, &col_count);
    if (sql_type == SQLX_CMD_SELECT && !col_info) {
        handle_error(sth, CUBRID_ER_CANNOT_GET_COLUMN_INFO, NULL);
        return -2;
    }

    imp_sth->col_info = col_info;
    imp_sth->sql_type = sql_type;
    imp_sth->col_count = col_count;

    switch (sql_type) {
    case SQLX_CMD_INSERT:
    case SQLX_CMD_UPDATE:
    case SQLX_CMD_DELETE:
    case SQLX_CMD_CALL:
    case SQLX_CMD_SELECT:
        imp_sth->affected_rows = res;
        break;
    default:
        imp_sth->affected_rows = -1;
    }

    if (sql_type == SQLX_CMD_SELECT) {
        res = cci_cursor (imp_sth->handle, 1, CCI_CURSOR_CURRENT, &error);
        if (res < 0 && res != CCI_ER_NO_MORE_DATA) {
            handle_error (sth, res, &error);
            return -2;
        }

        DBIc_NUM_FIELDS (imp_sth) = col_count;
        DBIc_ACTIVE_on (imp_sth);
    }
    
    return imp_sth->affected_rows;
}

/***************************************************************************
 *
 * Name:    dbd_st_fetch
 *
 * Purpose: Called for execute the prepared SQL statement; our part of
 *          the statement handle constructor
 *
 * Input:   sth - statement handle being initialized
 *          imp_sth - drivers private statement handle data
 *
 * Returns: array of columns; the array is allocated by DBI via
 *          DBIS->get_fbav(imp_sth), even the values of the array
 *          are prepared, we just need to modify them appropriately
 *
 **************************************************************************/

AV *
dbd_st_fetch( SV *sth, imp_sth_t *imp_sth )
{
    AV *av;
    int res;
    T_CCI_ERROR error;

    if (DBIc_ACTIVE(imp_sth)) {
        DBIc_ACTIVE_off(imp_sth);
    }

    res = cci_cursor (imp_sth->handle, 0, CCI_CURSOR_CURRENT, &error);
    if (res == CCI_ER_NO_MORE_DATA) {
        return Nullav;
    } else if (res < 0) {
        handle_error (sth, res, &error);
        return Nullav;
    }

    if ((res = cci_fetch (imp_sth->handle, &error)) < 0) {
        handle_error (sth, res, &error);
        return Nullav;
    }

    av = DBIS->get_fbav(imp_sth);
    if ((res = _cubrid_fetch_row (av, 
                                  imp_sth->handle, 
                                  imp_sth->col_count, 
                                  imp_sth->col_info, 
                                  &error)) < 0) {
        handle_error (sth, res, &error);
        return Nullav;
    }

    res = cci_cursor (imp_sth->handle, 1, CCI_CURSOR_CURRENT, &error);
    if (res < 0 && res != CCI_ER_NO_MORE_DATA) {
        handle_error (sth, res, &error);
        return Nullav;
    }

    return av;
}

/***************************************************************************
 *
 * Name:    dbd_st_finish
 *
 * Purpose: Called for freeing a CUBRID result
 *
 * Input:   sth - statement handle being finished
 *          imp_sth - drivers private statement handle data
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int
dbd_st_finish( SV *sth, imp_sth_t *imp_sth )
{
    if (!DBIc_ACTIVE(imp_sth))
        return TRUE;

    DBIc_ACTIVE_off(imp_sth);

    return TRUE;
}

/***************************************************************************
 *
 * Name:    dbd_st_destroy
 *
 * Purpose: Our part of the statement handles destructor
 *
 * Input:   sth - statement handle being destroyed
 *          imp_sth - drivers private statement handle data
 *
 * Returns: Nothing
 *
 **************************************************************************/

void
dbd_st_destroy( SV *sth, imp_sth_t *imp_sth )
{
    if (imp_sth->handle) {
        if (imp_sth->lob) {
            int i;
            for (i = 0; i < imp_sth->affected_rows; i++) {
                _cubrid_lob_free (imp_sth->lob[i].lob, imp_sth->lob[i].type);
            }

            free (imp_sth->lob);
            imp_sth->lob = NULL;
        }

        cci_close_req_handle (imp_sth->handle);
        imp_sth->handle = 0;

        imp_sth->col_count = -1;
        imp_sth->sql_type = 0;
        imp_sth->affected_rows = -1;
    }

    DBIc_IMPSET_off(imp_sth);

    return;
}

/***************************************************************************
 *
 * Name:    dbd_st_STORE_attrib
 *
 * Purpose: Modifies a statement handles attributes
 *
 * Input:   sth - statement handle being initialized
 *          imp_sth - drivers private statement handle data
 *          keysv - attribute name
 *          valuesv - attribute value
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int
dbd_st_STORE_attrib( SV *sth, imp_sth_t *imp_sth, SV *keysv, SV *valuesv )
{
    return TRUE;
}

/***************************************************************************
 *
 * Name:    dbd_st_FETCH_attrib
 *
 * Purpose: Retrieves a statement handles attributes
 *
 * Input:   sth - statement handle
 *          imp_sth - drivers private statement handle data
 *          keysv - attribute name
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

SV *
dbd_st_FETCH_attrib( SV *sth, imp_sth_t *imp_sth, SV *keysv )
{
    SV *retsv = Nullsv;
    STRLEN kl;
    char *key = SvPV (keysv, kl);
    int i;
    char col_name[128] = {'\0'};

    switch (kl) {
    case 4:
        if (strEQ ("NAME", key)) {
            AV *av = newAV ();
            retsv = newRV_inc (sv_2mortal ((SV *)av));
            for (i = 1; i<= imp_sth->col_count; i++) {
                strcpy (col_name, 
                        CCI_GET_RESULT_INFO_NAME (imp_sth->col_info, i));
                av_store (av, i-1, newSVpv (col_name, 0));
            }
        }
        else if (strEQ("TYPE", key)) {
            int type;
            AV *av = newAV ();
            retsv = newRV_inc (sv_2mortal ((SV *)av));
            for (i = 1; i<= imp_sth->col_count; i++) {
                type = CCI_GET_RESULT_INFO_TYPE (imp_sth->col_info, i);
                av_store (av, i-1, newSViv (type));
            }
        }
        break;
    case 5:
        if (strEQ ("SCALE", key)) {
            AV *av = newAV ();
            int scale;
            retsv = newRV_inc (sv_2mortal ((SV *)av));
            for (i = 1; i<= imp_sth->col_count; i++) {
                scale = CCI_GET_RESULT_INFO_SCALE (imp_sth->col_info, i);
                av_store (av, i-1, newSViv (scale));
            }
        }
        break;
    case 8:
        if (strEQ ("NULLABLE", key)) {
            AV *av = newAV ();
            int not_null;
            retsv = newRV_inc (sv_2mortal ((SV *)av));
            for (i = 1; i<= imp_sth->col_count; i++) {
                not_null = CCI_GET_RESULT_INFO_IS_NON_NULL (imp_sth->col_info, i) ? 0 : 1;
                av_store (av, i-1, newSViv (not_null));
            }
        }
        break;
    case 9:
        if (strEQ ("PRECISION", key)) {
            AV *av = newAV ();
            int precision;
            retsv = newRV_inc (sv_2mortal ((SV *)av));
            for (i = 1; i<= imp_sth->col_count; i++) {
                precision = CCI_GET_RESULT_INFO_PRECISION (imp_sth->col_info, i);
                av_store (av, i-1, newSViv (precision));
            }
        }
        break;
    }

    return sv_2mortal (retsv);
}

/***************************************************************************
 *
 * Name:    dbd_st_blob_read (Not implement now)
 *
 * Purpose: Used for blob reads if the statement handles blob 
 *
 * Input:   sth - statement handle from which a blob will be 
 *                fetched (currently not supported by DBD::cubrid)
 *          imp_sth - drivers private statement handle data
 *          field - field number of the blob
 *          offset - the offset of the field, where to start reading
 *          len - maximum number of bytes to read
 *          destrv - RV* that tells us where to store
 *          destoffset - destination offset
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int
dbd_st_blob_read(SV *sth, imp_sth_t *imp_sth,
        int field, long offset, long len, SV *destrv, long destoffset)
{
    sth = sth;
    imp_sth = imp_sth;
    field = field;
    offset = offset;
    len = len;
    destrv = destrv;
    destoffset = destoffset;
    return FALSE;
}

/***************************************************************************
 *
 * Name:    dbd_st_rows
 *
 * Purpose: used to get the number of rows affected by the SQL statement
 *          (INSERT, DELETE, UPDATE).
 *
 * Input:   sth - statement handle
 *          imp_sth - drivers private statement handle data
 *
 * Returns: Number of rows affected by the SQL statement for success.
 *          -1, when SQL statement is not INSERT, DELETE or UPDATE.
 *
 **************************************************************************/

int
dbd_st_rows( SV * sth, imp_sth_t * imp_sth )
{
    return imp_sth->affected_rows;
}

/***************************************************************************
 *
 * Name:    dbd_bind_ph
 *
 * Purpose: Binds a statement value to a parameter
 *
 * Input:   sth - statement handle
 *          imp_sth - drivers private statement handle data
 *          param - parameter number, counting starts with 1
 *          value - value being inserted for parameter "param"
 *          sql_type - SQL type of the value
 *          attribs - bind parameter attributes
 *          inout - TRUE, if parameter is an output variable (currently
 *                  this is not supported)
 *          maxlen - ???
 *
 * Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int
dbd_bind_ph( SV *sth, imp_sth_t *imp_sth, SV *param, SV *value,
             IV sql_type, SV *attribs, int is_inout, IV maxlen )
{
    int res;
    char *bind_value = NULL;
    STRLEN bind_value_len;
    T_CCI_ERROR error;

    int index = SvIV(param);
    if (index < 1 || index > DBIc_NUM_PARAMS(imp_sth)) {
        handle_error (sth, CCI_ER_BIND_INDEX, NULL);
        return FALSE;
    }

    bind_value = SvPV (value, bind_value_len);

    if (sql_type == SQL_BLOB || sql_type == SQL_CLOB) {

        if ((res = _cubrid_lob_bind (sth, 
                                     index, 
                                     sql_type, 
                                     bind_value, 
                                     &error)) < 0) {
            handle_error (sth, res, &error);
            return FALSE;
        }

        return TRUE;
    }


    if ((res = cci_bind_param (imp_sth->handle, 
                    index, CCI_A_TYPE_STR,
                    bind_value, CCI_U_TYPE_CHAR, 0)) < 0) {
        handle_error(sth, res, NULL);
        return FALSE;
    }

    return TRUE;
}

/* Large object functions */

/**************************************************************************/

int
cubrid_st_lob_get( SV *sth, int col )
{
    int res, ind;
    T_CCI_ERROR error;
    int i = 0;
    T_CCI_U_TYPE u_type;

    D_imp_sth (sth);

    if (col < 1 || col > DBIc_NUM_FIELDS (imp_sth)) {
        handle_error (sth, CCI_ER_COLUMN_INDEX, NULL);
        return FALSE;
    }

    if (imp_sth->sql_type != SQLX_CMD_SELECT) {
        handle_error (sth, CCI_ER_NO_MORE_DATA, NULL);
        return FALSE;
    }

    res = cci_cursor (imp_sth->handle, 0, CCI_CURSOR_CURRENT, &error);
    if (res == CCI_ER_NO_MORE_DATA) {
        return TRUE;
    }
    else if (res < 0) {
        handle_error (sth, res, &error);
        return FALSE;
    }

    u_type = CCI_GET_RESULT_INFO_TYPE (imp_sth->col_info, col);
    if (!(u_type == CCI_U_TYPE_BLOB ||  u_type == CCI_U_TYPE_CLOB)) {
        handle_error (sth, CUBRID_ER_NOT_LOB_TYPE, NULL);
        return FALSE;
    }

    imp_sth->lob = (T_CUBRID_LOB *) malloc (
            imp_sth->affected_rows * sizeof (T_CUBRID_LOB)
            );

    while (1) {

        if ((res = cci_fetch(imp_sth->handle, &error)) < 0) {
            handle_error (sth, res, &error);
            return FALSE;
        }

        if ( u_type == CCI_U_TYPE_BLOB) {
            imp_sth->lob[i].type = CCI_U_TYPE_BLOB;
            if ((res = cci_get_data (imp_sth->handle,
                                     col,
                                     CCI_A_TYPE_BLOB,
                                     (void *)&imp_sth->lob[i].lob,
                                     &ind)) < 0) {
                handle_error (sth, res, NULL);
                return FALSE;
            }
        }
        else {
            imp_sth->lob[i].type = CCI_U_TYPE_BLOB;
            if ((res = cci_get_data (imp_sth->handle,
                                     col,
                                     CCI_A_TYPE_CLOB,
                                     (void *)&imp_sth->lob[i].lob,
                                     (&ind))) < 0) {
                handle_error (sth, res, NULL);
                return FALSE;
            }
        }

        i++;

        res = cci_cursor (imp_sth->handle, 1, CCI_CURSOR_CURRENT, &error);
        if (res == CCI_ER_NO_MORE_DATA) {
            break;
        }
        else if (res < 0) {
            handle_error (sth, res, &error);
            return FALSE;
        }
    }

    return TRUE;
}

int
cubrid_st_lob_export( SV *sth, int index, char *filename )
{
    char buf[CUBRID_BUFFER_LEN] = {'\0'};
    int fd, res, size;
    long long pos = 0, lob_size;
    T_CCI_ERROR error;

    D_imp_sth (sth);

    if (imp_sth->lob[index-1].lob == NULL) {
        handle_error (sth, CCI_ER_INVALID_LOB_HANDLE, NULL);
        return FALSE;
    }

    if ((fd = open (filename, O_CREAT | O_WRONLY | O_TRUNC, 0666)) < 0) {
        handle_error (sth, CCI_ER_FILE, NULL);
        return FALSE;
    }

    lob_size = _cubrid_lob_size (imp_sth->lob[index-1].lob, imp_sth->lob[index-1].type);

    while (1) {
        if ((size = _cubrid_lob_read (imp_sth->conn, 
                                      imp_sth->lob[index-1].lob, 
                                      imp_sth->lob[index-1].type, 
                                      pos, 
                                      CUBRID_BUFFER_LEN, 
                                      buf, 
                                      &error)) < 0) {
            res = size;
            goto ER_LOB_EXPORT;
        }

        if ((res = write (fd, buf, size)) < 0) {
            res = CUBRID_ER_WRITE_FILE;
            goto ER_LOB_EXPORT;
        }

        pos += size;
        if (pos == lob_size) {
            break;
        }
    }

    close (fd);
    return TRUE;

ER_LOB_EXPORT:
    if (fd >= 0) {
        close (fd);
        unlink (filename);
    }

    handle_error (sth, res, &error);
    return FALSE;
}

int
cubrid_st_lob_import( SV *sth, 
                      int index,
                      char *filename,
                      IV sql_type )
{
    T_CCI_ERROR error;
    T_CCI_LOB lob;
    T_CCI_U_TYPE u_type;
    T_CCI_A_TYPE a_type;
    int fd, size, res;
    long long pos = 0;
    char buf[CUBRID_BUFFER_LEN] = {'\0'};

    D_imp_sth (sth);

    if (sql_type == SQL_BLOB) {
        u_type = CCI_U_TYPE_BLOB;
        a_type = CCI_A_TYPE_BLOB;
    }
    else if (sql_type == SQL_CLOB) {
        u_type = CCI_U_TYPE_CLOB;
        a_type = CCI_A_TYPE_CLOB;
    }
    else {
        handle_error (sth, CUBRID_ER_NOT_LOB_TYPE, NULL);
        return FALSE;
    }

    if ((res = _cubrid_lob_new (imp_sth->conn, 
                                &lob,
                                u_type, 
                                &error)) < 0 ) {
        handle_error (sth, res, &error);
        return FALSE;
    }

    if ((fd = open (filename, O_RDONLY, 0400)) < 0) {
        res = CCI_ER_FILE;
        goto ER_LOB_IMPORT;
    }

    while (1) {
        if ((size = read (fd, buf, CUBRID_BUFFER_LEN)) < 0) {
            res = CUBRID_ER_READ_FILE;
            goto ER_LOB_IMPORT;
        }
        
        if (size == 0) {
            break;
        }

        if ((res = _cubrid_lob_write (imp_sth->conn, 
                                      lob, 
                                      u_type,
                                      pos, 
                                      size, 
                                      buf, 
                                      &error)) < 0) {
            goto ER_LOB_IMPORT;
        }

        pos += size;
    }

    if ((res = cci_bind_param (imp_sth->handle, 
                               index, 
                               a_type, 
                               (void *)lob,
                               u_type, 
                               CCI_BIND_PTR)) < 0) {
        goto ER_LOB_IMPORT;
    }

    close (fd);
    return TRUE;

ER_LOB_IMPORT:
    if (fd >= 0) {
        close (fd);
    }

    _cubrid_lob_free (lob, u_type);
    handle_error (sth, res, &error);
    return FALSE;
}

int
cubrid_st_lob_close (SV *sth)
{
    D_imp_sth (sth);

    if (imp_sth->lob) {
        int i;
        for (i = 0; i < imp_sth->affected_rows; i++) {
            _cubrid_lob_free (imp_sth->lob[i].lob, imp_sth->lob[i].type);
        }

        free (imp_sth->lob);
        imp_sth->lob = NULL;
    }

    return TRUE;
}

static int
_cubrid_lob_bind( SV *sth, 
                  int index, 
                  IV sql_type,
                  char *buf, 
                  T_CCI_ERROR *error )
{
    T_CCI_LOB lob;
    T_CCI_U_TYPE u_type;
    T_CCI_A_TYPE a_type;
    int res;

    D_imp_sth (sth);
   
    if (sql_type == SQL_BLOB) {
        u_type = CCI_U_TYPE_BLOB;
        a_type = CCI_A_TYPE_BLOB;
    } else {
        u_type = CCI_U_TYPE_CLOB;
        a_type = CCI_A_TYPE_CLOB;
    }

    if ((res = _cubrid_lob_new (imp_sth->conn, 
                                &lob, 
                                u_type,
                                error)) < 0 ) {
        return res;
    }

    if ((res = _cubrid_lob_write (imp_sth->conn, 
                                  lob, 
                                  u_type,
                                  0, 
                                  strlen(buf), 
                                  buf, 
                                  error)) < 0) {
        _cubrid_lob_free (lob, u_type);
        return res;
    }

    if ((res = 
        cci_bind_param (imp_sth->handle, 
            index, a_type, (void *)lob, u_type, CCI_BIND_PTR)) < 0)
    {
        _cubrid_lob_free (lob, u_type);
        return res;
    }

    return 0;
}

static int 
_cubrid_lob_new( int conn, 
                 T_CCI_LOB *lob, 
                 T_CCI_U_TYPE type, 
                 T_CCI_ERROR *error )
{
    return (type == CCI_U_TYPE_BLOB) ? 
        cci_blob_new (conn, lob, error) : cci_clob_new (conn, lob, error);
}

static long long
_cubrid_lob_size( T_CCI_LOB lob, T_CCI_U_TYPE type )
{
    return (type == CCI_U_TYPE_BLOB) ? 
        cci_blob_size (lob) : cci_clob_size (lob);
}

static int
_cubrid_lob_write( int conn, 
                   T_CCI_LOB lob, 
                   T_CCI_U_TYPE type, 
                   long long start_pos, 
                   int length, 
                   const char *buf,
                   T_CCI_ERROR *error )
{
    return (type == CCI_U_TYPE_BLOB) ?
        cci_blob_write (conn, lob, start_pos, length, buf, error) :
        cci_clob_write (conn, lob, start_pos, length, buf, error);
}

static int
_cubrid_lob_read( int conn, 
                  T_CCI_LOB lob, 
                  T_CCI_U_TYPE type, 
                  long long start_pos, 
                  int length, 
                  char *buf, 
                  T_CCI_ERROR *error )
{
    return (type == CCI_U_TYPE_BLOB) ?
        cci_blob_read (conn, lob, start_pos, length, buf, error) :
        cci_clob_read (conn, lob, start_pos, length, buf, error);
}

static int 
_cubrid_lob_free( T_CCI_LOB lob, T_CCI_U_TYPE type )
{
    return (type == CCI_U_TYPE_BLOB) ?
        cci_blob_free (lob) : cci_clob_free (lob);
}

/* catalog functions */

/*************************************************************************/
SV *
_cubrid_primary_key( SV *dbh, char *table )
{
    int res, req_handle, col_count;
    T_CCI_COL_INFO *col_info;
    T_CCI_CUBRID_STMT sql_type;
    T_CCI_ERROR error;
    AV *rows_av;
    SV *rows_rvav;

    D_imp_dbh (dbh);

    if ((res = cci_schema_info (imp_dbh->handle, 
                                CCI_SCH_PRIMARY_KEY, 
                                table, 
                                NULL, 
                                0, 
                                &error)) < 0) {
        goto ER_CUBRID_PRIMARY_KEY;
    }

    req_handle = res;

    if (!(col_info = cci_get_result_info (req_handle, &sql_type, &col_count))) {
        handle_error (dbh, CUBRID_ER_CANNOT_GET_COLUMN_INFO, NULL);
        return Nullsv;
    }

    rows_av = newAV ();

    if ((res = _cubrid_fetch_schema (rows_av, 
                                     req_handle, 
                                     col_count, 
                                     col_info, 
                                     &error)) < 0) {
        goto ER_CUBRID_PRIMARY_KEY;
    }

    cci_close_req_handle (req_handle);
    rows_rvav = sv_2mortal(newRV_noinc((SV *)rows_av));
    return rows_rvav;

ER_CUBRID_PRIMARY_KEY:
    cci_close_req_handle (req_handle);
    if (rows_av != Nullav) {
        av_undef(rows_av);
    }
    handle_error (dbh, res, &error);
    return Nullsv;
}

SV *
_cubrid_foreign_key( SV *dbh, char *pk_table, char *fk_table)
{
    int res, req_handle, col_count;
    T_CCI_COL_INFO *col_info;
    T_CCI_CUBRID_STMT sql_type;
    T_CCI_ERROR error;
    AV *rows_av;
    SV *rows_rvav;

    D_imp_dbh (dbh);

    if (strcmp(pk_table, "") != 0  && strcmp (fk_table, "") != 0) {
        if ((res = cci_schema_info (imp_dbh->handle, 
                                      CCI_SCH_CROSS_REFERENCE,
                                      pk_table, 
                                      fk_table, 
                                      0, 
                                      &error)) < 0) {
            goto ER_CUBRID_FOREIGN_KEY;
        }
    }
    else if (strcmp(pk_table, "") != 0  && strcmp (fk_table, "") == 0) {
        if ((res = cci_schema_info (imp_dbh->handle, 
                                    CCI_SCH_EXPORTED_KEYS, 
                                    pk_table, 
                                    NULL, 
                                    0, 
                                    &error)) < 0) {
            goto ER_CUBRID_FOREIGN_KEY;
        }
    }
    else if (strcmp(pk_table, "") == 0  && strcmp (fk_table, "") != 0) {
        if ((res = cci_schema_info (imp_dbh->handle, 
                                    CCI_SCH_IMPORTED_KEYS, 
                                    fk_table,
                                    NULL,
                                    0,
                                    &error)) < 0) {
            goto ER_CUBRID_FOREIGN_KEY;
        }
    }

    req_handle = res;

    if (!(col_info = cci_get_result_info (req_handle, &sql_type, &col_count))) {
        handle_error (dbh, CUBRID_ER_CANNOT_GET_COLUMN_INFO, NULL);
        return Nullsv;
    }

    rows_av = newAV ();

    if ((res = _cubrid_fetch_schema (rows_av, 
                                     req_handle, 
                                     col_count, 
                                     col_info, 
                                     &error)) < 0) {
        goto ER_CUBRID_FOREIGN_KEY;
    }

    cci_close_req_handle (req_handle);
    rows_rvav = sv_2mortal(newRV_noinc((SV *)rows_av));
    return rows_rvav;

ER_CUBRID_FOREIGN_KEY:
    cci_close_req_handle (req_handle);
    if (rows_av != Nullav) {
        av_undef(rows_av);
    }
    handle_error (dbh, res, &error);
    return Nullsv;
}

static int
_cubrid_fetch_schema( AV *rows_av, 
                      int req_handle, 
                      int col_count, 
                      T_CCI_COL_INFO *col_info, 
                      T_CCI_ERROR *error )
{
    int res;

    while (1) {
        AV *copy_row, *fetch_av;
        int i = 0;

        res = cci_cursor (req_handle, 1, CCI_CURSOR_CURRENT, error);
        if (res == CCI_ER_NO_MORE_DATA) {
            break;
        }
        else if (res < 0) {
            return res;
        }

        if ((res = cci_fetch (req_handle, error)) < 0) {
            return res;
        }

        fetch_av = newAV();
        while (i < col_count) {
            av_store (fetch_av, i, newSV(0));
            i++;
        }

        if ((res = _cubrid_fetch_row (fetch_av,
                                      req_handle, 
                                      col_count, 
                                      col_info, 
                                      error)) < 0) {

            av_undef (fetch_av);
            return res;
        }

        copy_row = av_make (AvFILL(fetch_av) + 1, AvARRAY(fetch_av));
        av_push (rows_av, newRV_noinc ((SV *)copy_row));
        
        av_undef (fetch_av);
    }

    return 0;
}

static int
_cubrid_fetch_row( AV *av, 
                   int req_handle, 
                   int col_count, 
                   T_CCI_COL_INFO *col_info, 
                   T_CCI_ERROR *error )
{
    int i, res, type, num, ind;
    char *buf;
    double ddata;

    for (i = 0; i < col_count; i++) {
        SV *sv = AvARRAY(av)[i];

        type = CCI_GET_RESULT_INFO_TYPE (col_info, i+1);
        
        switch (type) {
        case CCI_U_TYPE_INT:
        case CCI_U_TYPE_SHORT:
            if ((res = cci_get_data (req_handle, 
                            i+1, CCI_A_TYPE_INT, &num, &ind)) < 0) {
                return res;
            }

            if (ind < 0) {
                (void) SvOK_off (sv);
            } else {
                sv_setiv (sv, num);
            }
            break;
        case CCI_U_TYPE_FLOAT:
        case CCI_U_TYPE_DOUBLE:
        case CCI_U_TYPE_NUMERIC:
            if ((res = cci_get_data (req_handle,
                            i+1, CCI_A_TYPE_DOUBLE, &ddata, &ind)) < 0) {
                return res;
            }

            if (ind < 0) {
                (void) SvOK_off (sv);
            } else {
                sv_setnv (sv, ddata);
            }
            break;
        default:
            if ((res = cci_get_data (req_handle,
                            i+1, CCI_A_TYPE_STR, &buf, &ind)) < 0) {
                return res;
            }
            if (ind < 0) {
                (void) SvOK_off (sv);
            } else {
                sv_setpvn (sv, buf, strlen(buf));
            }
        }
    }

    return 0;
}
