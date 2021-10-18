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

/*
 * cubrid_log.h -
 */

#ifndef _CUBRID_LOG_H_
#define _CUBRID_LOG_H_

#ident "$Id$"

#include <stdint.h>
#include <time.h>

#define CUBRID_LOG_UNAVAILABLE_CDC_SERVER        (-34)
#define CUBRID_LOG_FAILED_LOGIN                 (-33)
#define CUBRID_LOG_INVALID_PASSWORD             (-32)
#define CUBRID_LOG_INVALID_ID                   (-31)
#define CUBRID_LOG_FAILED_EXTRACT               (-30)
#define CUBRID_LOG_EXCEED_TRACELOG_FILESIZE     (-29)
#define CUBRID_LOG_FAILED_MALLOC                (-28)
#define CUBRID_LOG_INVALID_FUNC_CALL_STAGE      (-27)
#define CUBRID_LOG_INVALID_CONNECTION_TIMEOUT   (-26)
#define CUBRID_LOG_INVALID_EXTRACTION_TIMEOUT   (-25)
#define CUBRID_LOG_NO_FILE_PERMISSION           (-24)
#define CUBRID_LOG_INVALID_PATH                 (-23)
#define CUBRID_LOG_INVALID_LEVEL                (-22)
#define CUBRID_LOG_INVALID_FILESIZE             (-21)
#define CUBRID_LOG_INVALID_MAX_LOG_ITEM         (-20)
#define CUBRID_LOG_INVALID_RETRIEVE_ALL         (-19)
#define CUBRID_LOG_INVALID_CLASSOID             (-18)
#define CUBRID_LOG_INVALID_CLASSOID_ARR_SIZE    (-17)
#define CUBRID_LOG_INVALID_USER                 (-16)
#define CUBRID_LOG_INVALID_USER_ARR_SIZE        (-15)
#define CUBRID_LOG_CONNECTION_TIMEDOUT          (-14)
#define CUBRID_LOG_INVALID_DBNAME               (-13)
#define CUBRID_LOG_INVALID_HOST                 (-12)
#define CUBRID_LOG_INVALID_PORT                 (-11)
#define CUBRID_LOG_FAILED_CONNECT               (-10)
#define CUBRID_LOG_INVALID_TIMESTAMP            (-9)
#define CUBRID_LOG_INVALID_OUT_PARAM            (-8)
#define CUBRID_LOG_LSA_NOT_FOUND                (-7)
#define CUBRID_LOG_EXTRACTION_TIMEOUT           (-6)
#define CUBRID_LOG_INVALID_LSA                  (-5)
#define CUBRID_LOG_INVALID_LOGITEM_LIST         (-4)
#define CUBRID_LOG_FAILED_DISCONNECT            (-3)
#define CUBRID_LOG_FAILED_CLOSE_FILE            (-2)
#define CUBRID_LOG_FAILED_DEALLOC               (-1)
#define CUBRID_LOG_SUCCESS                      (0)
#define CUBRID_LOG_SUCCESS_WITH_NO_LOGITEM      (1)
#define CUBRID_LOG_SUCCESS_WITH_ADJUSTED_LSA    (2)

#define CUBRID_LOG_MAX_DBNAME_LEN  64

typedef struct ddl DDL;
struct ddl
{
  int ddl_type;
  int object_type;
  uint64_t oid;
  uint64_t classoid;
  char *statement;
  int statement_length;
};

typedef struct dml DML;
struct dml
{
  int dml_type;
  uint64_t classoid;
  int num_changed_column;
  int *changed_column_index;
  char **changed_column_data;
  int *changed_column_data_len;
  int num_cond_column;
  int *cond_column_index;
  char **cond_column_data;
  int *cond_column_data_len;
};

typedef struct dcl DCL;
struct dcl
{
  int dcl_type;
  time_t timestamp;
};

typedef struct timer TIMER;
struct timer
{
  time_t timestamp;
};

typedef union cubrid_data_item CUBRID_DATA_ITEM;
union cubrid_data_item
{
  DDL ddl;
  DML dml;
  DCL dcl;
  TIMER timer;
};

typedef struct cubrid_log_item CUBRID_LOG_ITEM;
struct cubrid_log_item
{
  int transaction_id;
  char *user;
  int data_item_type;
  CUBRID_DATA_ITEM data_item;
  CUBRID_LOG_ITEM *next;
};

#ifdef __cplusplus
extern "C"
{
#endif
/* API for the configuration step */
  extern int cubrid_log_set_connection_timeout (int timeout);
  extern int cubrid_log_set_extraction_timeout (int timeout);
  extern int cubrid_log_set_tracelog (char *path, int level, int filesize);
  extern int cubrid_log_set_max_log_item (int max_log_item);
  extern int cubrid_log_set_all_in_cond (int retrieve_all);
  extern int cubrid_log_set_extraction_table (uint64_t * classoid_arr, int arr_size);
  extern int cubrid_log_set_extraction_user (char **user_arr, int arr_size);

/* API for the preparation step */
  extern int cubrid_log_connect_server (char *host, int port, char *dbname, char *id, char *password);
  extern int cubrid_log_find_lsa (time_t * timestamp, uint64_t * lsa);

/* API for the extraction step */
  extern int cubrid_log_extract (uint64_t * lsa, CUBRID_LOG_ITEM ** log_item_list, int *list_size);
  extern int cubrid_log_clear_log_item (CUBRID_LOG_ITEM * log_item_list);

/* API for the finalization step */
  extern int cubrid_log_finalize (void);
#ifdef __cplusplus
}
#endif

#endif				/* _CUBRID_LOG_H_ */
