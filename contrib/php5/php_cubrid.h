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

#ifndef PHP_CUBRID_H
#define PHP_CUBRID_H

#ifdef PHP_WIN32
#   define PHP_CUBRID_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#   define PHP_CUBRID_API __attribute__ ((visibility("default")))
#else
#   define PHP_CUBRID_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#include <cas_cci.h>

typedef struct
{
    int facility;
    int code;
    char msg[1024];
} T_CUBRID_ERROR;

extern zend_module_entry cubrid_module_entry;
#define cubrid_module_ptr &cubrid_module_entry

ZEND_MINIT_FUNCTION(cubrid);
ZEND_MSHUTDOWN_FUNCTION(cubrid);
ZEND_RINIT_FUNCTION(cubrid);
ZEND_RSHUTDOWN_FUNCTION(cubrid);
ZEND_MINFO_FUNCTION(cubrid);

ZEND_FUNCTION(cubrid_version);
ZEND_FUNCTION(cubrid_connect);
ZEND_FUNCTION(cubrid_pconnect);
ZEND_FUNCTION(cubrid_connect_with_url);
ZEND_FUNCTION(cubrid_pconnect_with_url);
ZEND_FUNCTION(cubrid_disconnect);
ZEND_FUNCTION(cubrid_close);
ZEND_FUNCTION(cubrid_prepare);
ZEND_FUNCTION(cubrid_bind);
ZEND_FUNCTION(cubrid_execute);
ZEND_FUNCTION(cubrid_next_result);
ZEND_FUNCTION(cubrid_affected_rows);
ZEND_FUNCTION(cubrid_close_request);
ZEND_FUNCTION(cubrid_fetch);
ZEND_FUNCTION(cubrid_current_oid);
ZEND_FUNCTION(cubrid_column_types);
ZEND_FUNCTION(cubrid_column_names);
ZEND_FUNCTION(cubrid_move_cursor);
ZEND_FUNCTION(cubrid_num_rows);
ZEND_FUNCTION(cubrid_num_cols);
ZEND_FUNCTION(cubrid_get);
ZEND_FUNCTION(cubrid_put);
ZEND_FUNCTION(cubrid_drop);
ZEND_FUNCTION(cubrid_is_instance);
ZEND_FUNCTION(cubrid_get_class_name);
ZEND_FUNCTION(cubrid_lock_read);
ZEND_FUNCTION(cubrid_lock_write);
ZEND_FUNCTION(cubrid_schema);
ZEND_FUNCTION(cubrid_col_size);
ZEND_FUNCTION(cubrid_col_get);
ZEND_FUNCTION(cubrid_set_add);
ZEND_FUNCTION(cubrid_set_drop);
ZEND_FUNCTION(cubrid_seq_drop);
ZEND_FUNCTION(cubrid_seq_insert);
ZEND_FUNCTION(cubrid_seq_put);
ZEND_FUNCTION(cubrid_set_autocommit);
ZEND_FUNCTION(cubrid_get_autocommit);
ZEND_FUNCTION(cubrid_commit);
ZEND_FUNCTION(cubrid_rollback);
ZEND_FUNCTION(cubrid_error_msg);
ZEND_FUNCTION(cubrid_error_code);
ZEND_FUNCTION(cubrid_error_code_facility);
ZEND_FUNCTION(cubrid_errno);
ZEND_FUNCTION(cubrid_error);
ZEND_FUNCTION(cubrid_field_name);
ZEND_FUNCTION(cubrid_field_table);
ZEND_FUNCTION(cubrid_field_type);
ZEND_FUNCTION(cubrid_field_flags);
ZEND_FUNCTION(cubrid_data_seek);
ZEND_FUNCTION(cubrid_fetch_array);
ZEND_FUNCTION(cubrid_fetch_assoc);
ZEND_FUNCTION(cubrid_fetch_row);
ZEND_FUNCTION(cubrid_fetch_field);
ZEND_FUNCTION(cubrid_num_fields);
ZEND_FUNCTION(cubrid_free_result);
ZEND_FUNCTION(cubrid_field_len);
ZEND_FUNCTION(cubrid_fetch_object);
ZEND_FUNCTION(cubrid_fetch_lengths);
ZEND_FUNCTION(cubrid_field_seek);
ZEND_FUNCTION(cubrid_result);
ZEND_FUNCTION(cubrid_unbuffered_query);
ZEND_FUNCTION(cubrid_query);
ZEND_FUNCTION(cubrid_get_charset);
ZEND_FUNCTION(cubrid_client_encoding);
ZEND_FUNCTION(cubrid_get_client_info);
ZEND_FUNCTION(cubrid_get_server_info);
ZEND_FUNCTION(cubrid_real_escape_string);
ZEND_FUNCTION(cubrid_get_db_parameter);
ZEND_FUNCTION(cubrid_set_db_parameter);
ZEND_FUNCTION(cubrid_list_dbs);
ZEND_FUNCTION(cubrid_db_name);
ZEND_FUNCTION(cubrid_insert_id);
ZEND_FUNCTION(cubrid_ping);
ZEND_FUNCTION(cubrid_lob_get);
ZEND_FUNCTION(cubrid_lob_size);
ZEND_FUNCTION(cubrid_lob_export);
ZEND_FUNCTION(cubrid_lob_send);
ZEND_FUNCTION(cubrid_lob_close);

ZEND_FUNCTION(cubrid_get_query_timeout);
ZEND_FUNCTION(cubrid_set_query_timeout);

ZEND_BEGIN_MODULE_GLOBALS(cubrid)
    T_CUBRID_ERROR recent_error;

    int last_connect_id;
    int last_request_id;
    T_CCI_CUBRID_STMT last_request_stmt_type;
    int last_request_affected_rows;

    char *default_userid, *default_passwd;
ZEND_END_MODULE_GLOBALS(cubrid)

#ifdef ZTS
#define CUBRID_G(v) TSRMG(cubrid_globals_id, zend_cubrid_globals *, v)
#else
#define CUBRID_G(v) (cubrid_globals.v)
#endif

#define phpext_cubrid_ptr cubrid_module_ptr

#endif /* PHP_CUBRID_H */
