/*
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

#ifndef _CUB_MIGRATE_
#define _CUB_MIGRATE_

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <string.h>
#include <pwd.h>
#include <dlfcn.h>
#include <errno.h>
#include <libgen.h>
#include <dbi.h>

#define VERSION "1.0"

#define BUF_LEN 2048
#define DB_CLIENT_TYPE_ADMIN_UTILITY 7

#define PRINT_LOG(fmt, ...) print_log (fmt "\tin %s () from %s:%d", __VA_ARGS__, __func__, __FILE__, __LINE__)

/* global variable */
const char *PRO_NAME = NULL;
const char *CUBRID_ENV = NULL;

void *dl_handle = NULL;

/* CUBRID function pointer */
int (*cub_db_restart_ex) (const char *, const char *, const char *, const char *, const char *, int);
int (*cub_er_errid) (void);
DB_SESSION *(*cub_db_open_buffer) (const char *);
int (*cub_db_compile_statement) (DB_SESSION *);
int (*cub_db_execute_statement_local) (DB_SESSION *, int, DB_QUERY_RESULT **);
void (*cub_db_close_session) (DB_SESSION *);
int (*cub_db_query_end) (DB_QUERY_RESULT *);
int (*cub_db_commit_transaction) (void);
int (*cub_db_abort_transaction) (void);
int (*cub_db_query_get_tuple_value) (DB_QUERY_RESULT * result, int tuple_index, DB_VALUE * value);
int (*cub_db_query_next_tuple) (DB_QUERY_RESULT * result);
int (*cub_db_query_first_tuple) (DB_QUERY_RESULT * result);
char *(*cub_db_get_database_version) (void);
int (*cub_db_shutdown) (void);
const char *(*cub_db_error_string) (int);

/* CUBRID global variable */
int *cub_Au_disable;
bool *cub_Au_sysadm;

#endif
