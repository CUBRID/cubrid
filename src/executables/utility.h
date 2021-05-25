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
 * utility.h : Message constant definitions used by the utility
 *
 */

#ifndef _UTILITY_H_
#define _UTILITY_H_

#include <stdio.h>

#include "config.h"
#include "cubrid_getopt.h"
#include "util_func.h"
#include "dynamic_array.h"

/*
 * UTILITY MESSAGE SETS
 */

/*
 * Message set id in the message catalog MSGCAT_CATALOG_UTILS.
 * These define the $set numbers within the catalog file of the message
 * for each utility.
 */
typedef enum
{
  MSGCAT_UTIL_SET_GENERIC = 1,
  MSGCAT_UTIL_SET_BACKUPDB = 2,
  MSGCAT_UTIL_SET_COPYDB = 3,
  MSGCAT_UTIL_SET_CREATEDB = 4,
  MSGCAT_UTIL_SET_DELETEDB = 6,
  MSGCAT_UTIL_SET_RENAMEDB = 7,
  MSGCAT_UTIL_SET_MASTER = 9,
  MSGCAT_UTIL_SET_OPTIMIZEDB = 10,
  MSGCAT_UTIL_SET_RESTOREDB = 11,
  MSGCAT_UTIL_SET_LOADDB = 12,
  MSGCAT_UTIL_SET_UNLOADDB = 13,
  MSGCAT_UTIL_SET_COMPACTDB = 14,
  MSGCAT_UTIL_SET_COMMDB = 15,
  MSGCAT_UTIL_SET_PATCHDB = 16,
  MSGCAT_UTIL_SET_ADDVOLDB = 17,
  MSGCAT_UTIL_SET_CHECKDB = 18,
  MSGCAT_UTIL_SET_SPACEDB = 19,
  MSGCAT_UTIL_SET_ESTIMATEDB_DATA = 20,
  MSGCAT_UTIL_SET_ESTIMATEDB_INDEX = 21,
  MSGCAT_UTIL_SET_INSTALLDB = 22,
  MSGCAT_UTIL_SET_MIGDB = 23,
  MSGCAT_UTIL_SET_DIAGDB = 24,
  MSGCAT_UTIL_SET_LOCKDB = 25,
  MSGCAT_UTIL_SET_KILLTRAN = 26,
  MSGCAT_UTIL_SET_ALTERDBHOST = 33,
  MSGCAT_UTIL_SET_LOADJAVA = 34,
  MSGCAT_UTIL_SET_PLANDUMP = 37,
  MSGCAT_UTIL_SET_PARAMDUMP = 38,
  MSGCAT_UTIL_SET_CHANGEMODE = 39,
  MSGCAT_UTIL_SET_COPYLOGDB = 40,
  MSGCAT_UTIL_SET_APPLYLOGDB = 41,
  MSGCAT_UTIL_SET_LOGFILEDUMP = 42,
  MSGCAT_UTIL_SET_STATDUMP = 43,
  MSGCAT_UTIL_SET_APPLYINFO = 44,
  MSGCAT_UTIL_SET_ACLDB = 45,
  MSGCAT_UTIL_SET_GENLOCALE = 46,
  MSGCAT_UTIL_SET_DUMPLOCALE = 47,
  MSGCAT_UTIL_SET_SYNCCOLLDB = 48,
  MSGCAT_UTIL_SET_TRANLIST = 49,
  MSGCAT_UTIL_SET_GEN_TZ = 51,
  MSGCAT_UTIL_SET_DUMP_TZ = 52,
  MSGCAT_UTIL_SET_RESTORESLAVE = 53,
  MSGCAT_UTIL_SET_DELVOLDB = 54,
  MSGCAT_UTIL_SET_VACUUMDB = 55,
  MSGCAT_UTIL_SET_CHECKSUMDB = 56,
  MSGCAT_UTIL_SET_TDE = 57,
} MSGCAT_UTIL_SET;

/* Message id in the set MSGCAT_UTIL_SET_GENERIC */
typedef enum
{
  MSGCAT_UTIL_GENERIC_BAD_DATABASE_NAME = 1,
  MSGCAT_UTIL_GENERIC_BAD_OUTPUT_FILE = 2,
  MSGCAT_UTIL_GENERIC_BAD_VOLUME_NAME = 6,
  MSGCAT_UTIL_GENERIC_VERSION = 9,
  MSGCAT_UTIL_GENERIC_ADMIN_USAGE = 10,
  MSGCAT_UTIL_GENERIC_SERVICE_INVALID_NAME = 12,
  MSGCAT_UTIL_GENERIC_SERVICE_INVALID_CMD = 13,
  MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL = 14,
  MSGCAT_UTIL_GENERIC_START_STOP_3S = 15,
  MSGCAT_UTIL_GENERIC_START_STOP_2S = 16,
  MSGCAT_UTIL_GENERIC_NOT_RUNNING_2S = 17,
  MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S = 18,
  MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_2S = 19,
  MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S = 20,
  MSGCAT_UTIL_GENERIC_RESULT = 21,
  MSGCAT_UTIL_GENERIC_MISS_ARGUMENT = 22,
  MSGCAT_UTIL_GENERIC_CUBRID_USAGE = 23,
  MSGCAT_UTIL_GENERIC_ARGS_OVER = 31,
  MSGCAT_UTIL_GENERIC_MISS_DBNAME = 32,
  MSGCAT_UTIL_GENERIC_DEPRECATED = 33,
  MSGCAT_UTIL_GENERIC_INVALID_PARAMETER = 34,
  MSGCAT_UTIL_GENERIC_NO_MEM = 35,
  MSGCAT_UTIL_GENERIC_NOT_HA_MODE = 36,
  MSGCAT_UTIL_GENERIC_HA_MODE = 37,
  MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB = 38,
  MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_NODE = 39,
  MSGCAT_UTIL_GENERIC_INVALID_CMD = 40,
  MSGCAT_UTIL_GENERIC_MANAGER_NOT_INSTALLED = 41,
  MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT = 42,
  MSGCAT_UTIL_GENERIC_FILEOPEN_ERROR = 43
} MSGCAT_UTIL_GENERIC_MSG;

/* Message id in the set MSGCAT_UTIL_SET_DELETEDB */
typedef enum
{
  DELETEDB_MSG_USAGE = 60
} MSGCAT_DELETEDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_BACKUPDB */
typedef enum
{
  BACKUPDB_INVALID_THREAD_NUM_OPT = 30,
  BACKUPDB_INVALID_PATH = 31,
  BACKUPDB_USING_SEPARATE_KEYS = 32,
  BACKUPDB_NOT_USING_SEPARATE_KEYS = 33,
  BACKUPDB_FIFO_KEYS_NOT_SUPPORTED = 34,
  BACKUPDB_MSG_USAGE = 60
} MSGCAT_BACKUPDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_RENAMEDB */
typedef enum
{
  RENAMEDB_VOLEXT_PATH_INVALID = 31,
  RENAMEDB_VOLS_TOFROM_PATHS_FILE_INVALID = 32,
  RENAMEDB_MSG_USAGE = 60
} MSGCAT_RENAMEDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_OPTIMIZEDB */
typedef enum
{
  OPTIMIZEDB_MSG_USAGE = 60
} MSGCAT_OPTIMIZEDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_COMMDB */
typedef enum
{
  COMMDB_STRING1 = 21,
  COMMDB_STRING2 = 22,
  COMMDB_STRING3 = 23,
  COMMDB_STRING4 = 24,
  COMMDB_STRING5 = 25,
  COMMDB_STRING6 = 26,
  COMMDB_STRING7 = 27,
  COMMDB_STRING8 = 28,
  COMMDB_STRING9 = 29,
  COMMDB_STRING10 = 30,
  COMMDB_STRING11 = 31,
  COMMDB_STRING12 = 32,
  COMMDB_STRING13 = 33,
  COMMDB_STRING14 = 34,
  COMMDB_INVALID_IMMEDIATELY_OPTION = 39
} MSGCAT_COMMDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_COPYDB */
typedef enum
{
  COPYDB_MSG_IDENTICAL = 30,
  COPYDB_VOLEXT_PATH_INVALID = 31,
  COPYDB_VOLS_TOFROM_PATHS_FILE_INVALID = 32,
  COPYDB_MSG_USAGE = 60
} MSGCAT_COPYDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_CREATEDB */
typedef enum
{
  CREATEDB_MSG_MISSING_USER = 41,
  CREATEDB_MSG_UNKNOWN_CMD = 42,
  CREATEDB_MSG_BAD_OUTPUT = 43,
  CREATEDB_MSG_CREATING = 45,
  CREATEDB_MSG_FAILURE = 46,
  CREATEDB_MSG_BAD_USERFILE = 47,
  CREATEDB_MSG_BAD_RANGE = 48,
  CREATEDB_MSG_INVALID_SIZE = 49,
  CREATEDB_MSG_USAGE = 60
} MSGCAT_CREATEDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_MASTER */
typedef enum
{
  MASTER_MSG_DUPLICATE = 11,
  MASTER_MSG_STARTING = 12,
  MASTER_MSG_EXITING = 13,
  MASTER_MSG_NO_PARAMETERS = 15,
  MASTER_MSG_PROCESS_ERROR = 16,
  MASTER_MSG_SERVER_STATUS = 17,
  MASTER_MSG_SERVER_NOTIFIED = 18,
  MASTER_MSG_SERVER_NOT_FOUND = 19,
  MASTER_MSG_GOING_DOWN = 20,
  MASTER_MSG_FAILOVER_FINISHED = 21
} MSGCAT_MASTER_MSG;

/* Message id in the set MSGCAT_UTIL_SET_RESTOREDB */
typedef enum
{
  RESTOREDB_MSG_BAD_DATE = 19,
  RESTOREDB_MSG_FAILURE = 20,
  RESTOREDB_MSG_USAGE = 60
} MSGCAT_RESTOREDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_ADDVOLDB */
typedef enum
{
  ADDVOLDB_MSG_BAD_NPAGES = 20,
  ADDVOLDB_MSG_BAD_PURPOSE = 21,
  ADDVOLDB_INVALID_MAX_WRITESIZE_IN_SEC = 22,
  ADDVOLDB_MSG_USAGE = 60
} MSGCAT_ADDVOLDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_CHECKDB */
typedef enum
{
  CHECKDB_MSG_INCONSISTENT = 20,
  CHECKDB_MSG_NO_SUCH_CLASS = 21,
  CHECKDB_MSG_NO_SUCH_INDEX = 22,
  CHECKDB_MSG_USAGE = 60
} MSGCAT_CHECKDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_SPACEDB */
typedef enum
{
  SPACEDB_OUTPUT_TITLE = 10,
  SPACEDB_MSG_BAD_OUTPUT = 11,
  SPACEDB_OUTPUT_TITLE_LOB = 12,

  SPACEDB_MSG_ALL_HEADER_PAGES = 20,
  SPACEDB_MSG_ALL_HEADER_SIZE = 21,
  SPACEDB_MSG_PERM_PERM_FORMAT = 22,
  SPACEDB_MSG_PERM_TEMP_FORMAT = 23,
  SPACEDB_MSG_TEMP_TEMP_FORMAT = 24,
  SPACEDB_MSG_TOTAL_FORMAT = 25,

  SPACEDB_MSG_VOLS_TITLE = 30,
  SPACEDB_MSG_VOLS_HEADER_PAGES = 31,
  SPACEDB_MSG_VOLS_HEADER_SIZE = 32,
  SPACEDB_MSG_VOLS_PERM_PERM_FORMAT = 33,
  SPACEDB_MSG_VOLS_PERM_TEMP_FORMAT = 34,
  SPACEDB_MSG_VOLS_TEMP_TEMP_FORMAT = 35,

  SPACEDB_MSG_FILES_TITLE = 40,
  SPACEDB_MSG_FILES_HEADER_PAGES = 41,
  SPACEDB_MSG_FILES_HEADER_SIZE = 42,
  SPACEDB_MSG_FILES_FORMAT = 43,

  SPACEDB_MSG_END_UNDERLINE = 50,
  SPACEDB_MSG_USAGE = 60
} MSGCAT_SPACEDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_ESTIMATEDB_DATA */
typedef enum
{
  ESTIMATEDB_DATA_MSG_NPAGES = 15,
  ESTIMATEDB_DATA_MSG_USAGE = 60
} MSGCAT_ESTIMATEDB_DATA_MSG;

/* Message id in the set MSGCAT_UTIL_SET_ESTIMATEDB_INDEX */
typedef enum
{
  ESTIMATEDB_INDEX_BAD_KEYTYPE = 15,
  ESTIMATEDB_INDEX_BAD_KEYLENGTH = 16,
  ESTIMATEDB_INDEX_BAD_ARGUMENTS = 17,
  ESTIMATEDB_INDEX_MSG_NPAGES = 20,
  ESTIMATEDB_INDEX_MSG_BLT_NPAGES = 21,
  ESTIMATEDB_INDEX_MSG_BLT_WRS_NPAGES = 22,
  ESTIMATEDB_INDEX_MSG_INPUT = 23,
  ESTIMATEDB_INDEX_MSG_INSTANCES = 24,
  ESTIMATEDB_INDEX_MSG_NUMBER_KEYS = 25,
  ESTIMATEDB_INDEX_MSG_AVG_KEYSIZE = 26,
  ESTIMATEDB_INDEX_MSG_KEYTYPE = 27,
  ESTIMATEDB_INDEX_MSG_USAGE = 60
} MSGCAT_ESTIMATEDB_INDEX_MSG;

/* Message id in the set MSGCAT_UTIL_SET_DIAGDB */
typedef enum
{
  DIAGDB_MSG_BAD_OUTPUT = 15,
  DIAGDB_MSG_USAGE = 60
} MSGCAT_DIAGDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_ALTERDBHOST */
typedef enum
{
  ALTERDBHOST_MSG_USAGE = 60
} MSGCAT_ALTERDBHOST_MSG;

/* Message id in the set MSGCAT_UTIL_SET_PATCHDB */
typedef enum
{
  PATCHDB_MSG_USAGE = 60
} MSGCAT_PATCHDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_INSTALLDB */
typedef enum
{
  INSTALLDB_MSG_USAGE = 60
} MSGCAT_INSTALLDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_LOCKDB */
typedef enum
{
  LOCKDB_MSG_BAD_OUTPUT = 15,
  LOCKDB_MSG_NOT_IN_STANDALONE = 59,
  LOCKDB_MSG_USAGE = 60
} MSGCAT_LOCKDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_TRANLIST */
typedef enum
{
  TRANLIST_MSG_USER_PASSWORD = 20,
  TRANLIST_MSG_SUMMARY_HEADER = 22,
  TRANLIST_MSG_SUMMARY_UNDERSCORE = 23,
  TRANLIST_MSG_SUMMARY_ENTRY = 24,
  TRANLIST_MSG_NONE_TABLE_ENTRIES = 25,
  TRANLIST_MSG_NOT_DBA_USER = 26,
  TRANLIST_MSG_INVALID_SORT_KEY = 27,
  TRANLIST_MSG_QUERY_INFO_HEADER = 32,
  TRANLIST_MSG_QUERY_INFO_ENTRY = 33,
  TRANLIST_MSG_QUERY_INFO_UNDERSCORE = 34,
  TRANLIST_MSG_FULL_INFO_HEADER = 42,
  TRANLIST_MSG_FULL_INFO_ENTRY = 43,
  TRANLIST_MSG_FULL_INFO_UNDERSCORE = 44,
  TRANLIST_MSG_TRAN_INDEX = 45,
  TRANLIST_MSG_SQL_ID = 46,
  TRANLIST_MSG_NOT_IN_STANDALONE = 59,
  TRANLIST_MSG_USAGE = 60
} MSGCAT_TRANLIST_MSG;

/* Message id in the set MSGCAT_UTIL_SET_KILLTRAN */
typedef enum
{
  KILLTRAN_MSG_MANY_ARGS = 20,
  KILLTRAN_MSG_DBA_PASSWORD = 21,
  KILLTRAN_MSG_NO_MATCHES = 26,
  KILLTRAN_MSG_READY_TO_KILL = 27,
  KILLTRAN_MSG_VERIFY = 28,
  KILLTRAN_MSG_KILLING = 29,
  KILLTRAN_MSG_KILL_FAILED = 30,
  KILLTRAN_MSG_KILL_TIMEOUT = 31,
  KILLTRAN_MSG_INVALID_TRANINDEX = 32,
  KILLTRAN_MSG_NOT_IN_STANDALONE = 59,
  KILLTRAN_MSG_USAGE = 60
} MSGCAT_KILLTRAN_MSG;

/* Message id in the set MSGCAT_UTIL_SET_PLANDUMP */
typedef enum
{
  PLANDUMP_MSG_BAD_OUTPUT = 15,
  PLANDUMP_MSG_NOT_IN_STANDALONE = 59,
  PLANDUMP_MSG_USAGE = 60
} MSGCAT_PLANDUMP_MSG;

/* Message id in the set MSGCAT_UTIL_SET_LOADJAVA */
typedef enum
{
  LOADJAVA_ARG_FORCE_OVERWRITE = 5,
  LOADJAVA_ARG_FORCE_OVERWRITE_HELP = 6
} MSGCAT_LOADJAVA_MSG;

/* Message id in the set MSGCAT_UTIL_SET_COMPACTDB */
typedef enum
{
  COMPACTDB_MSG_PASS1 = 11,
  COMPACTDB_MSG_PROCESSED = 12,
  COMPACTDB_MSG_PASS2 = 13,
  COMPACTDB_MSG_CLASS = 14,
  COMPACTDB_MSG_OID = 15,
  COMPACTDB_MSG_INSTANCES = 16,
  COMPACTDB_MSG_UPDATING = 17,
  COMPACTDB_MSG_REFOID = 18,
  COMPACTDB_MSG_CANT_TRANSFORM = 19,
  COMPACTDB_MSG_NO_HEAP = 20,
  COMPACTDB_MSG_CANT_UPDATE = 21,
  COMPACTDB_MSG_FAILED = 22,
  COMPACTDB_MSG_ALREADY_STARTED = 23,
  COMPACTDB_MSG_OUT_OF_RANGE_PAGES = 24,
  COMPACTDB_MSG_OUT_OF_RANGE_INSTANCE_LOCK_TIMEOUT = 25,
  COMPACTDB_MSG_TOTAL_OBJECTS = 26,
  COMPACTDB_MSG_FAILED_OBJECTS = 27,
  COMPACTDB_MSG_MODIFIED_OBJECTS = 28,
  COMPACTDB_MSG_BIG_OBJECTS = 29,
  COMPACTDB_MSG_REPR_DELETED = 30,
  COMPACTDB_MSG_REPR_CANT_DELETE = 31,
  COMPACTDB_MSG_ISOLATION_LEVEL_FAILURE = 32,
  COMPACTDB_MSG_FAILURE = 33,
  COMPACTDB_MSG_OUT_OF_RANGE_CLASS_LOCK_TIMEOUT = 34,
  COMPACTDB_MSG_LOCKED_CLASS = 35,
  COMPACTDB_MSG_INVALID_CLASS = 36,
  COMPACTDB_MSG_PROCESS_CLASS_ERROR = 37,
  COMPACTDB_MSG_NOTHING_TO_PROCESS = 38,
  COMPACTDB_MSG_INVALID_PARAMETERS = 39,
  COMPACTDB_MSG_UNKNOWN_CLASS_NAME = 40,
  COMPACTDB_MSG_RECLAIMED = 41,
  COMPACTDB_MSG_RECLAIM_SKIPPED = 42,
  COMPACTDB_MSG_RECLAIM_ERROR = 43,
  COMPACTDB_MSG_PASS3 = 44,
  COMPACTDB_MSG_HEAP_COMPACT_FAILED = 45,
  COMPACTDB_MSG_HEAP_COMPACT_SUCCEEDED = 46
} MSGCAT_COMPACTDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_UNLOADDB */
typedef enum
{
  UNLOADDB_MSG_INVALID_CACHED_PAGES = 41,
  UNLOADDB_MSG_INVALID_CACHED_PAGE_SIZE = 42,
  UNLOADDB_MSG_OBJECTS_DUMPED = 43,
  UNLOADDB_MSG_OBJECTS_FAILED = 46,
  UNLOADDB_MSG_INVALID_DIR_NAME = 47,
  UNLOADDB_MSG_LOG_LSA = 48,
  UNLOADDB_MSG_PASSWORD_PROMPT = 49
} MSGCAT_UNLOADDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_LOADDB */
typedef enum
{
  LOADDB_MSG_MISSING_DBNAME = 19,
  LOADDB_MSG_UNREACHABLE_LINE = 20,
  LOADDB_MSG_SIG1 = 21,
  LOADDB_MSG_INTERRUPTED_COMMIT = 22,
  LOADDB_MSG_INTERRUPTED_ABORT = 23,
  LOADDB_MSG_BAD_INFILE = 24,
  LOADDB_MSG_CHECKING = 25,
  LOADDB_MSG_ERROR_COUNT = 26,
  LOADDB_MSG_INSERTING = 27,
  LOADDB_MSG_OBJECT_COUNT = 28,
  LOADDB_MSG_DEFAULT_COUNT = 29,
  LOADDB_MSG_COMMITTING = 30,
  LOADDB_MSG_CLOSING = 31,
  LOADDB_MSG_LINE = 32,
  LOADDB_MSG_PARSE_ERROR = 33,
  LOADDB_MSG_MISSING_DOMAIN = 34,
  LOADDB_MSG_SET_DOMAIN_ERROR = 35,
  LOADDB_MSG_UNEXPECTED_SET = 36,
  LOADDB_MSG_UNEXPECTED_TYPE = 37,
  LOADDB_MSG_UNKNOWN_ATT_CLASS = 38,
  LOADDB_MSG_UNKNOWN_CLASS = 39,
  LOADDB_MSG_UNKNOWN_CLASS_ID = 40,
  LOADDB_MSG_UNAUTHORIZED_CLASS = 41,
  LOADDB_MSG_STOPPED = 42,
  LOADDB_MSG_UPDATE_WARNING = 43,
  LOADDB_MSG_REDEFINING_INSTANCE = 44,
  LOADDB_MSG_INSTANCE_DEFINED = 45,
  LOADDB_MSG_INSTANCE_RESERVED = 46,
  LOADDB_MSG_UNIQUE_VIOLATION_NULL = 47,
  LOADDB_MSG_INSTANCE_COUNT = 48,
  LOADDB_MSG_CLASS_TITLE = 49,
  LOADDB_MSG_PASSWORD_PROMPT = 50,
  LOADDB_MSG_UPDATING_STATISTICS = 51,
  LOADDB_MSG_STD_ERR = 52,
  LOADDB_MSG_LEX_ERROR = 53,
  LOADDB_MSG_SYNTAX_ERR = 54,
  LOADDB_MSG_SYNTAX_MISSING = 55,
  LOADDB_MSG_SYNTAX_IN = 56,
  LOADDB_MSG_INCOMPATIBLE_ARGS = 57,
  LOADDB_MSG_COMMITTED_INSTANCES = 58,
  LOADDB_MSG_NOPT_ERR = 59,
  LOADDB_MSG_CONVERSION_ERROR = 60,
  LOADDB_MSG_OID_NOT_SUPPORTED = 61,
  LOADDB_MSG_UPDATED_CLASS_STATS = 62,
#ifndef DISABLE_TTA_FIX
  LOADDB_MSG_INSTANCE_COUNT_EX = 112,
#endif
  LOADDB_MSG_LAST_COMMITTED_LINE = 113,
  LOADDB_MSG_INSERT_AND_FAIL_COUNT = 116,
  LOADDB_MSG_LOAD_FAIL = 117,
  LOADDB_MSG_EXCEED_MAX_LEN = 118,
  LOADDB_MSG_OBJECTS_SYNTAX_CHECKED = 119,
  LOADDB_MSG_TABLE_IS_MISSING = 120,
  LOADDB_MSG_IGNORED_CLASS = 121,

  LOADDB_MSG_USAGE = 122
} MSGCAT_LOADDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_MIGDB */
typedef enum
{
  MIGDB_MSG_TEMPORARY_CLASS_OID = 1,
  MIGDB_MSG_CANT_PRINT_ELO = 2,
  MIGDB_MSG_CANT_ACCESS_LO = 3,
  MIGDB_MSG_CANT_OPEN_LO_FILE = 4,
  MIGDB_MSG_READ_ERROR = 5,
  MIGDB_MSG_WRITE_ERROR = 6,
  MIGDB_MSG_CANT_OPEN_ELO = 7,
  MIGDB_MSG_FH_HASH_FILENAME = 9,
  MIGDB_MSG_FH_NAME = 10,
  MIGDB_MSG_FH_SIZE = 11,
  MIGDB_MSG_FH_PAGE_SIZE = 12,
  MIGDB_MSG_FH_DATA_SIZE = 13,
  MIGDB_MSG_FH_ENTRY_SIZE = 14,
  MIGDB_MSG_FH_ENTRIES_PER_PAGE = 15,
  MIGDB_MSG_FH_CACHED_PAGES = 16,
  MIGDB_MSG_FH_NUM_ENTRIES = 17,
  MIGDB_MSG_FH_NUM_COLLISIONS = 18,
  MIGDB_MSG_FH_HASH_FILENAME2 = 19,
  MIGDB_MSG_FH_NEXT_OVERFLOW_ENTRY = 20,
  MIGDB_MSG_FH_KEY_TYPE = 21,
  MIGDB_MSG_FH_PAGE_HEADERS = 22,
  MIGDB_MSG_FH_LAST_PAGE_HEADER = 23,
  MIGDB_MSG_FH_FREE_PAGE_HEADER = 24,
  MIGDB_MSG_FH_PAGE_BITMAP = 25,
  MIGDB_MSG_FH_PAGE_BITMAP_SIZE = 26
} MSGCAT_MIGDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_PARAMDUMP */
typedef enum
{
  PARAMDUMP_MSG_BAD_OUTPUT = 11,
  PARAMDUMP_MSG_CLIENT_PARAMETER = 21,
  PARAMDUMP_MSG_SERVER_PARAMETER = 22,
  PARAMDUMP_MSG_STANDALONE_PARAMETER = 23,
  PARAMDUMP_MSG_USAGE = 60
} MSGCAT_PARAMDUMP_MSG;

/* Message id in the set MSGCAT_UTIL_SET_CHANGEMODE */
typedef enum
{
  CHANGEMODE_MSG_BAD_MODE = 11,
  CHANGEMODE_MSG_CANNOT_CHANGE = 12,
  CHANGEMODE_MSG_DBA_PASSWORD = 21,
  CHANGEMODE_MSG_SERVER_MODE = 22,
  CHANGEMODE_MSG_SERVER_MODE_CHANGED = 23,
  CHANGEMODE_MSG_NOT_HA_MODE = 24,
  CHANGEMODE_MSG_HA_NOT_SUPPORT = 58,
  CHANGEMODE_MSG_NOT_IN_STANDALONE = 59,
  CHANGEMODE_MSG_USAGE = 60
} MSGCAT_CHANGEMODE_MSG;

/* Message id in the set MSGCAT_UTIL_SET_COPYLOGDB */
typedef enum
{
  COPYLOGDB_MSG_BAD_MODE = 11,
  COPYLOGDB_MSG_DBA_PASSWORD = 21,
  COPYLOGDB_MSG_NOT_HA_MODE = 22,
  COPYLOGDB_MSG_HA_NOT_SUPPORT = 58,
  COPYLOGDB_MSG_NOT_IN_STANDALONE = 59,
  COPYLOGDB_MSG_USAGE = 60
} MSGCAT_COPYLOGDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_APPLYLOGDB */
typedef enum
{
  APPLYLOGDB_MSG_DBA_PASSWORD = 21,
  APPLYLOGDB_MSG_NOT_HA_MODE = 22,
  APPLYLOGDB_MSG_HA_NOT_SUPPORT = 58,
  APPLYLOGDB_MSG_NOT_IN_STANDALONE = 59,
  APPLYLOGDB_MSG_USAGE = 60
} MSGCAT_APPLYLOGDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_STATMDUMP */
typedef enum
{
  STATDUMP_MSG_BAD_OUTPUT = 11,
  STATDUMP_MSG_NOT_IN_STANDALONE = 59,
  STATDUMP_MSG_USAGE = 60
} MSGCAT_STATDUMP_MSG;

/* Message id in the set MSGCAT_UTIL_SET_APPLYINFO */
typedef enum
{
  APPLYINFO_MSG_DBA_PASSWORD = 21,
  APPLYINFO_MSG_NOT_HA_MODE = 22,
  APPLYINFO_MSG_HA_NOT_SUPPORT = 58,
  APPLYINFO_MSG_NOT_IN_STANDALONE = 59,
  APPLYINFO_MSG_USAGE = 60
} MSGCAT_APPLYINFO_MSG;

/* Message id in the set MSGCAT_UTIL_SET_ACLDB */
typedef enum
{
  ACLDB_MSG_NOT_IN_STANDALONE = 59,
  ACLDB_MSG_USAGE = 60
} MSGCAT_ACLDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_GENLOCALE */
typedef enum
{
  GENLOCALE_MSG_USAGE = 60,
  GENLOCALE_MSG_INVALID_LOCALE = 61
} MSGCAT_GENLOCALE_MSG;

/* Message id in the set MSGCAT_UTIL_SET_DUMPLOCALE */
typedef enum
{
  DUMPLOCALE_MSG_INCOMPAT_INPUT_SEL = 57,
  DUMPLOCALE_MSG_INVALID_CP_RANGE = 58,
  DUMPLOCALE_MSG_INVALID_LOCALE = 59,
  DUMPLOCALE_MSG_USAGE = 60
} MSGCAT_DUMPLOCALE_MSG;

/* Message id in the set MSGCAT_UTIL_SET_SYNCCOLLDB */
typedef enum
{
  SYNCCOLLDB_MSG_PARTITION_OBS_COLL = 43,
  SYNCCOLLDB_MSG_FK_OBS_COLL = 44,
  SYNCCOLLDB_MSG_CLASS_OBS_COLL = 45,
  SYNCCOLLDB_MSG_FI_OBS_COLL = 46,
  SYNCCOLLDB_MSG_SYNC_ABORT = 47,
  SYNCCOLLDB_MSG_SYNC_OK = 48,
  SYNCCOLLDB_MSG_SYNC_CONTINUE = 49,
  SYNCCOLLDB_MSG_OBS_COLL = 50,
  SYNCCOLLDB_MSG_TRIG_OBS_COLL = 51,
  SYNCCOLLDB_MSG_VIEW_OBS_COLL = 52,
  SYNCCOLLDB_MSG_ATTR_OBS_COLL = 53,
  SYNCCOLLDB_MSG_REPORT_SQL_FILE = 54,
  SYNCCOLLDB_MSG_REPORT_NOT_NEEDED = 55,
  SYNCCOLLDB_MSG_REPORT_SYNC_REQUIRED = 56,
  SYNCCOLLDB_MSG_REPORT_NEW_COLL = 57,
  SYNCCOLLDB_MSG_REPORT_DB_OBS_OK = 58,
  SYNCCOLLDB_MSG_REPORT_DB_OBS_NOK = 59,
  SYNCCOLLDB_MSG_USAGE = 60
} MSGCAT_SYNCCOLLDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_GEN_TZ */
typedef enum
{
  GEN_TZ_MSG_INVALID_MODE = 59,
  GEN_TZ_MSG_INVALID_INPUT_FOLDER = 60,
  GEN_TZ_MSG_USAGE = 61
} MSGCAT_GEN_TZ_MSG;

/* Message id in the set MSGCAT_UTIL_SET_DUMP_TZ */
typedef enum
{
  DUMP_TZ_MSG_ID_OUT_OF_RANGE = 59,
  DUMP_TZ_MSG_USAGE = 60
} MSGCAT_DUMP_TZ_MSG;

/* Message id in the set MSGCAT_UTIL_SET_RESTORESLAVE */
typedef enum
{
  RESTORESLAVE_MSG_FAILURE = 20,
  RESTORESLAVE_MSG_HA_CATALOG_FAIL = 21,
  RESTORESLAVE_MSG_INVAILD_OPTIONS = 22,
  RESTORESLAVE_MSG_INVAILD_STATE = 23,
  RESTORESLAVE_MSG_USAGE = 60
} MSGCAT_RESTORESLAVE_MSG;

/* Message id in the set MSGCAT_UTIL_SET_DELVOLDB */
typedef enum
{
  DELVOLDB_MSG_READY_TO_DEL = 21,
  DELVOLDB_MSG_VERIFY = 22,
  DELVOLDB_MSG_CANNOT_REMOVE_FIRST_VOL = 31,
  DELVOLDB_MSG_CANNOT_FIND_VOL = 32,
  DELVOLDB_MSG_TOO_MANY_VOLID = 33,
  DELVOL_MSG_INVALID_VOLUME_ID = 34,
  DELVOLDB_MSG_USAGE = 60
} MSGCAT_DELVOLDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_VACUUMDB */
typedef enum
{
  VACUUMDB_MSG_CLIENT_SERVER_NOT_AVAILABLE = 20,
  VACUUMDB_MSG_FAILED = 21,
  VACUUMDB_MSG_BAD_OUTPUT = 22,
  VACUUMDB_MSG_USAGE = 60
} MSGCAT_VACUUMDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_CHECKSUMDB */
typedef enum
{
  CHECKSUMDB_MSG_INVALID_INPUT_FILE = 1,
  CHECKSUMDB_MSG_MUST_RUN_ON_ACTIVE = 2,
  CHECKSUMDB_MSG_HA_NOT_SUPPORT = 58,
  CHECKSUMDB_MSG_NOT_IN_STANDALONE = 59,
  CHECKSUMDB_MSG_USAGE = 60
} MSGCAT_CHECKSUMDB_MSG;

/* Message id in the set MSGCAT_UTIL_SET_TDE */
typedef enum
{
  TDE_MSG_DBA_PASSWORD = 21,
  TDE_MSG_NO_SET_MK_INFO = 25,
  TDE_MSG_MK_CHANGING = 26,
  TDE_MSG_MK_CHANGED = 27,
  TDE_MSG_MK_SET_ON_DATABASE_DELETE = 28,
  TDE_MSG_MK_DELETED = 29,
  TDE_MSG_MK_GENERATED = 30,
  TDE_MSG_USAGE = 60
} MSGCAT_TDE_MSG;

typedef void *DSO_HANDLE;

typedef enum
{
  CREATEDB,
  RENAMEDB,
  COPYDB,
  DELETEDB,
  BACKUPDB,
  RESTOREDB,
  ADDVOLDB,
#if 0
  DELVOLDB,
#endif
  SPACEDB,
  LOCKDB,
  KILLTRAN,
  OPTIMIZEDB,
  INSTALLDB,
  DIAGDB,
  PATCHDB,
  CHECKDB,
  ALTERDBHOST,
  PLANDUMP,
  ESTIMATE_DATA,
  ESTIMATE_INDEX,
  LOADDB,
  UNLOADDB,
  COMPACTDB,
  PARAMDUMP,
  STATDUMP,
  CHANGEMODE,
  COPYLOGDB,
  APPLYLOGDB,
  APPLYINFO,
  ACLDB,
  GENLOCALE,
  DUMPLOCALE,
  SYNCCOLLDB,
  TRANLIST,
  GEN_TZ,
  DUMP_TZ,
  RESTORESLAVE,
  VACUUMDB,
  CHECKSUMDB,
  TDE,
  LOGFILEDUMP,
} UTIL_INDEX;

typedef enum
{
  SA_ONLY,
  CS_ONLY,
  SA_CS
} UTIL_MODE;

typedef enum
{
  ARG_INTEGER,
  ARG_STRING,
  ARG_BOOLEAN,
  ARG_BIGINT
} UTIL_ARG_TYPE;

typedef struct
{
  int arg_ch;
  union
  {
    int value_type;		/* if arg_ch is not OPTION_STRING_TABLE */
    int num_strings;		/* if arg_ch is OPTION_STRING_TABLE */
  } value_info;
  union
  {
    void *p;
    int i;
    INT64 l;
  } arg_value;
} UTIL_ARG_MAP;

typedef struct
{
  int utility_index;
  int utility_type;
  int need_args_num;
  const char *utility_name;
  const char *function_name;
  GETOPT_LONG *getopt_long;
  UTIL_ARG_MAP *arg_map;
} UTIL_MAP;

typedef struct _node_config
{
  char *node_name;
  char *copy_log_base;
  char *copy_sync_mode;
  int apply_max_mem_size;
} HA_NODE_CONF;

typedef struct _ha_config
{
  char **db_names;

  int num_node_conf;
  HA_NODE_CONF *node_conf;
} HA_CONF;

#define OPTION_STRING_TABLE                     10000

#if defined(WINDOWS)
#define UTIL_EXE_EXT            ".exe"
#else
#define UTIL_EXE_EXT            ""
#endif

#if defined(WINDOWS)
#define UTIL_WIN_SERVICE_CONTROLLER_NAME	"ctrlService" UTIL_EXE_EXT
#endif

#define UTIL_MASTER_NAME        "cub_master" UTIL_EXE_EXT
#define UTIL_COMMDB_NAME        "cub_commdb" UTIL_EXE_EXT
#define UTIL_CUBRID_NAME        "cub_server" UTIL_EXE_EXT
#define UTIL_BROKER_NAME        "cubrid_broker" UTIL_EXE_EXT
#define UTIL_MONITOR_NAME       "broker_monitor" UTIL_EXE_EXT
#define UTIL_TESTER_NAME        "broker_tester" UTIL_EXE_EXT
#define UTIL_CUB_MANAGER_NAME   "cub_manager" UTIL_EXE_EXT
#define UTIL_ADMIN_NAME         "cub_admin" UTIL_EXE_EXT
#define UTIL_SQLX_NAME          "sqlx" UTIL_EXE_EXT
#define UTIL_CSQL_NAME          "csql" UTIL_EXE_EXT
#define UTIL_CUBRID_REL_NAME    "cubrid_rel" UTIL_EXE_EXT
#define UTIL_OLD_COMMDB_NAME    "commdb" UTIL_EXE_EXT
#define UTIL_CUBRID             "cubrid" UTIL_EXE_EXT
#define UTIL_COPYLOGDB          "copylogdb" UTIL_EXE_EXT
#define UTIL_APPLYLOGDB         "applylogdb" UTIL_EXE_EXT
#define UTIL_JAVASP_NAME        "cub_javasp" UTIL_EXE_EXT

#define PROPERTY_ON             "on"
#define PROPERTY_OFF            "off"


#define PRINT_SERVICE_NAME	"cubrid service"
#define PRINT_MASTER_NAME       "cubrid master"
#define PRINT_SERVER_NAME       "cubrid server"
#define PRINT_BROKER_NAME       "cubrid broker"
#define PRINT_MANAGER_NAME      "cubrid manager server"
#define PRINT_HEARTBEAT_NAME    "cubrid heartbeat"
#define PRINT_JAVASP_NAME       "cubrid javasp"
#define PRINT_HA_PROCS_NAME     "HA processes"

#define PRINT_CMD_SERVICE       "service"
#define PRINT_CMD_BROKER        "broker"
#define PRINT_CMD_MANAGER       "manager"
#define PRINT_CMD_SERVER        "server"
#define PRINT_CMD_JAVASP        "javasp"

#define PRINT_CMD_START         "start"
#define PRINT_CMD_STOP          "stop"
#define PRINT_CMD_STATUS        "status"
#define PRINT_CMD_DEREG         "deregister"
#define PRINT_CMD_LIST          "list"
#define PRINT_CMD_RELOAD        "reload"
#define PRINT_CMD_ACL           "acl"
#define PRINT_CMD_COPYLOGDB     "copylogdb"
#define PRINT_CMD_APPLYLOGDB    "applylogdb"
#define PRINT_CMD_GETID         "getid"
#define PRINT_CMD_TEST          "test"
#define PRINT_CMD_REPLICATION	"replication"

#define PRINT_RESULT_SUCCESS    "success"
#define PRINT_RESULT_FAIL       "fail"

#define CHECK_SERVER            "Server"
#define CHECK_HA_SERVER         "HA-Server"

#define COMMDB_SERVER_STOP      "-S"
#define COMMDB_SERVER_STATUS    "-P"
#define COMMDB_ALL_STATUS       "-O"
#define COMMDB_ALL_STOP         "-A"
#define COMMDB_HA_DEREG_BY_PID  "-D"
#define COMMDB_HA_DEREG_BY_ARGS "-R"
#define COMMDB_HA_ALL_STOP      "-d"
#define COMMDB_IS_REG           "-C"
#define COMMDB_HA_NODE_LIST     "-N"
#define COMMDB_HA_PROC_LIST     "-L"
#define COMMDB_HA_PING_HOST_LIST "-p"
#define COMMDB_HA_RELOAD        "-F"
#define COMMDB_HA_DEACT_STOP_ALL          "--deact-stop-all"
#define COMMDB_HA_DEACT_CONFIRM_STOP_ALL  "--deact-confirm-stop-all"
#define COMMDB_HA_DEACT_CONFIRM_NO_SERVER "--deact-confirm-no-server"
#define COMMDB_HA_DEACTIVATE              "--deactivate-heartbeat"
#define COMMDB_HA_ACTIVATE                "--activate-heartbeat"
#define COMMDB_HOST                       "-h"
#define COMMDB_HB_DEACT_IMMEDIATELY       "-i"
#define COMMDB_HA_ADMIN_INFO              "--admin-info"
#define COMMDB_VERBOSE_OUTPUT             "--verbose"
#define COMMDB_HA_START_UTIL_PROCESS	  "-t"

#define ACLDB_RELOAD            "-r"

#define MASK_ALL                0xFF
#define MASK_SERVICE            0x01
#define MASK_SERVER             0x02
#define MASK_BROKER             0x04
#define MASK_MANAGER            0x08
#define MASK_ADMIN              0x20
#define MASK_HEARTBEAT          0x40
#define MASK_JAVASP             0x80

/* utility option list */
#define UTIL_OPTION_CREATEDB                    "createdb"
#define UTIL_OPTION_RENAMEDB                    "renamedb"
#define UTIL_OPTION_COPYDB                      "copydb"
#define UTIL_OPTION_DELETEDB                    "deletedb"
#define UTIL_OPTION_BACKUPDB                    "backupdb"
#define UTIL_OPTION_RESTOREDB                   "restoredb"
#define UTIL_OPTION_ADDVOLDB                    "addvoldb"
#if 0
#define UTIL_OPTION_DELVOLDB                    "delvoldb"
#endif
#define UTIL_OPTION_SPACEDB                     "spacedb"
#define UTIL_OPTION_LOCKDB                      "lockdb"
#define UTIL_OPTION_TRANLIST                    "tranlist"
#define UTIL_OPTION_KILLTRAN                    "killtran"
#define UTIL_OPTION_OPTIMIZEDB                  "optimizedb"
#define UTIL_OPTION_INSTALLDB                   "installdb"
#define UTIL_OPTION_DIAGDB                      "diagdb"
#define UTIL_OPTION_PATCHDB                     "emergency_patchlog"
#define UTIL_OPTION_CHECKDB                     "checkdb"
#define UTIL_OPTION_ALTERDBHOST                 "alterdbhost"
#define UTIL_OPTION_PLANDUMP                    "plandump"
#define UTIL_OPTION_ESTIMATE_DATA               "estimate_data"
#define UTIL_OPTION_ESTIMATE_INDEX              "estimate_index"
#define UTIL_OPTION_LOADDB                      "loaddb"
#define UTIL_OPTION_UNLOADDB                    "unloaddb"
#define UTIL_OPTION_COMPACTDB                   "compactdb"
#define UTIL_OPTION_PARAMDUMP                   "paramdump"
#define UTIL_OPTION_STATDUMP                    "statdump"
#define UTIL_OPTION_CHANGEMODE                  "changemode"
#define UTIL_OPTION_COPYLOGDB                   "copylogdb"
#define UTIL_OPTION_APPLYLOGDB                  "applylogdb"
#define UTIL_OPTION_LOGFILEDUMP                 "logfiledump"
#define UTIL_OPTION_APPLYINFO                   "applyinfo"
#define UTIL_OPTION_ACLDB			"acldb"
#define UTIL_OPTION_GENERATE_LOCALE		"genlocale"
#define UTIL_OPTION_DUMP_LOCALE			"dumplocale"
#define UTIL_OPTION_SYNCCOLLDB			"synccolldb"
#define UTIL_OPTION_GEN_TZ			"gen_tz"
#define UTIL_OPTION_DUMP_TZ			"dump_tz"
#define UTIL_OPTION_RESTORESLAVE                "restoreslave"
#define UTIL_OPTION_VACUUMDB			"vacuumdb"
#define UTIL_OPTION_CHECKSUMDB			"checksumdb"
#define UTIL_OPTION_TDE			        "tde"

#define HIDDEN_CS_MODE_S                        15000

/* createdb option list */
#define CREATE_PAGES_S                          'p'
#define CREATE_PAGES_L                          "pages"
#define CREATE_COMMENT_S                        10102
#define CREATE_COMMENT_L                        "comment"
#define CREATE_FILE_PATH_S                      'F'
#define CREATE_FILE_PATH_L                      "file-path"
#define CREATE_LOG_PATH_S                       'L'
#define CREATE_LOG_PATH_L                       "log-path"
#define CREATE_LOB_PATH_S                       'B'
#define CREATE_LOB_PATH_L                       "lob-base-path"
#define CREATE_SERVER_NAME_S                    10105
#define CREATE_SERVER_NAME_L                    "server-name"
#define CREATE_REPLACE_S                        'r'
#define CREATE_REPLACE_L                        "replace"
#define CREATE_MORE_VOLUME_FILE_S               10107
#define CREATE_MORE_VOLUME_FILE_L               "more-volume-file"
#define CREATE_USER_DEFINITION_FILE_S           10108
#define CREATE_USER_DEFINITION_FILE_L           "user-definition-file"
#define CREATE_CSQL_INITIALIZATION_FILE_S       10109
#define CREATE_CSQL_INITIALIZATION_FILE_L       "csql-initialization-file"
#define CREATE_OUTPUT_FILE_S                    'o'
#define CREATE_OUTPUT_FILE_L                    "output-file"
#define CREATE_VERBOSE_S                        'v'
#define CREATE_VERBOSE_L                        "verbose"
#define CREATE_CHARSET_S                        10112
#define CREATE_CHARSET_L                        "charset"
#define CREATE_LOG_PAGE_COUNT_S                 'l'
#define CREATE_LOG_PAGE_COUNT_L                 "log-page-count"
#define CREATE_PAGE_SIZE_S                      's'
#define CREATE_PAGE_SIZE_L                      "page-size"
#define CREATE_LOG_PAGE_SIZE_S                  10113
#define CREATE_LOG_PAGE_SIZE_L                  "log-page-size"
#define CREATE_DB_PAGE_SIZE_S                   10114
#define CREATE_DB_PAGE_SIZE_L                   "db-page-size"
#define CREATE_DB_VOLUME_SIZE_S                 10115
#define CREATE_DB_VOLUME_SIZE_L                 "db-volume-size"
#define CREATE_LOG_VOLUME_SIZE_S                10116
#define CREATE_LOG_VOLUME_SIZE_L                "log-volume-size"

/* renamedb option list */
#define RENAME_EXTENTED_VOLUME_PATH_S           'E'
#define RENAME_EXTENTED_VOLUME_PATH_L           "extended-volume-path"
#define RENAME_CONTROL_FILE_S                   'i'
#define RENAME_CONTROL_FILE_L                   "control-file"
#define RENAME_DELETE_BACKUP_S                  'd'
#define RENAME_DELETE_BACKUP_L                  "delete-backup"

/* copydb option list */
#define COPY_SERVER_NAME_S                      10300
#define COPY_SERVER_NAME_L                      "server-name"
#define COPY_FILE_PATH_S                        'F'
#define COPY_FILE_PATH_L                        "file-path"
#define COPY_LOG_PATH_S                         'L'
#define COPY_LOG_PATH_L                         "log-path"
#define COPY_EXTENTED_VOLUME_PATH_S             'E'
#define COPY_EXTENTED_VOLUME_PATH_L             "extended-volume-path"
#define COPY_CONTROL_FILE_S                     'i'
#define COPY_CONTROL_FILE_L                     "control-file"
#define COPY_REPLACE_S                          'r'
#define COPY_REPLACE_L                          "replace"
#define COPY_DELETE_SOURCE_S                    'd'
#define COPY_DELETE_SOURCE_L                    "delete-source"
#define COPY_LOB_PATH_S				'B'
#define COPY_LOB_PATH_L				"lob-base-path"
#define COPY_COPY_LOB_PATH_S			10308
#define COPY_COPY_LOB_PATH_L			"copy-lob-path"

/* deletedb option list */
#define DELETE_OUTPUT_FILE_S                    'o'
#define DELETE_OUTPUT_FILE_L                    "output-file"
#define DELETE_DELETE_BACKUP_S                  'd'
#define DELETE_DELETE_BACKUP_L                  "delete-backup"

/* backupdb option list */
#define BACKUP_DESTINATION_PATH_S               'D'
#define BACKUP_DESTINATION_PATH_L		"destination-path"
#define BACKUP_REMOVE_ARCHIVE_S                 'r'
#define BACKUP_REMOVE_ARCHIVE_L                 "remove-archive"
#define BACKUP_LEVEL_S                          'l'
#define BACKUP_LEVEL_L                          "level"
#define BACKUP_OUTPUT_FILE_S                    'o'
#define BACKUP_OUTPUT_FILE_L                    "output-file"
#define BACKUP_SA_MODE_S                        'S'
#define BACKUP_SA_MODE_L                        "SA-mode"
#define BACKUP_CS_MODE_S                        'C'
#define BACKUP_CS_MODE_L                        "CS-mode"
#define BACKUP_NO_CHECK_S                       10506
#define BACKUP_NO_CHECK_L                       "no-check"
#define BACKUP_THREAD_COUNT_S                   't'
#define BACKUP_THREAD_COUNT_L                   "thread-count"
#define BACKUP_COMPRESS_S                       'z'
#define BACKUP_COMPRESS_L                       "compress"
#define BACKUP_EXCEPT_ACTIVE_LOG_S              'e'
#define BACKUP_EXCEPT_ACTIVE_LOG_L              "except-active-log"
#define BACKUP_SLEEP_MSECS_S                    10600
#define BACKUP_SLEEP_MSECS_L                    "sleep-msecs"
#define BACKUP_SEPARATE_KEYS_S                  'k'
#define BACKUP_SEPARATE_KEYS_L                  "separate-keys"


/* restoredb option list */
#define RESTORE_UP_TO_DATE_S                    'd'
#define RESTORE_UP_TO_DATE_L                    "up-to-date"
#define RESTORE_LIST_S                          10601
#define RESTORE_LIST_L                          "list"
#define RESTORE_BACKUP_FILE_PATH_S              'B'
#define RESTORE_BACKUP_FILE_PATH_L              "backup-file-path"
#define RESTORE_LEVEL_S                         'l'
#define RESTORE_LEVEL_L                         "level"
#define RESTORE_PARTIAL_RECOVERY_S              'p'
#define RESTORE_PARTIAL_RECOVERY_L              "partial-recovery"
#define RESTORE_OUTPUT_FILE_S                   'o'
#define RESTORE_OUTPUT_FILE_L                   "output-file"
#define RESTORE_USE_DATABASE_LOCATION_PATH_S    'u'
#define RESTORE_USE_DATABASE_LOCATION_PATH_L    "use-database-location-path"
#define RESTORE_KEYS_FILE_PATH_S                'k'
#define RESTORE_KEYS_FILE_PATH_L                "keys-file-path"

/* addvoldb option list */
#define ADDVOL_VOLUME_NAME_S                    'n'
#define ADDVOL_VOLUME_NAME_L                    "volume-name"
#define ADDVOL_FILE_PATH_S                      'F'
#define ADDVOL_FILE_PATH_L                      "file-path"
#define ADDVOL_COMMENT_S                        10702
#define ADDVOL_COMMENT_L                        "comment"
#define ADDVOL_PURPOSE_S                        'p'
#define ADDVOL_PURPOSE_L                        "purpose"
#define ADDVOL_SA_MODE_S                        'S'
#define ADDVOL_SA_MODE_L                        "SA-mode"
#define ADDVOL_CS_MODE_S                        'C'
#define ADDVOL_CS_MODE_L                        "CS-mode"
#define ADDVOL_VOLUME_SIZE_S                    10706
#define ADDVOL_VOLUME_SIZE_L                    "db-volume-size"
#define ADDVOL_MAX_WRITESIZE_IN_SEC_S           10707
#define ADDVOL_MAX_WRITESIZE_IN_SEC_L           "max-writesize-in-sec"

#if 0
/* delvoldb option list */
#define DELVOL_VOLUME_ID_S                      'i'
#define DELVOL_VOLUME_ID_L                      "volume-id"
#define DELVOL_CLEAR_CACHE_S                    'c'
#define DELVOL_CLEAR_CACHE_L                    "clear-cache"
#define DELVOL_FORCE_S                          'f'
#define DELVOL_FORCE_L                          "force"
#define DELVOL_DBA_PASSWORD_S                   'p'
#define DELVOL_DBA_PASSWORD_L                   "dba-password"
#define DELVOL_SA_MODE_S                        'S'
#define DELVOL_SA_MODE_L                        "SA-mode"
#define DELVOL_CS_MODE_S                        'C'
#define DELVOL_CS_MODE_L                        "CS-mode"
#endif

/* spacedb option list */
#define SPACE_OUTPUT_FILE_S                     'o'
#define SPACE_OUTPUT_FILE_L                     "output-file"
#define SPACE_SA_MODE_S                         'S'
#define SPACE_SA_MODE_L                         "SA-mode"
#define SPACE_CS_MODE_S                         'C'
#define SPACE_CS_MODE_L                         "CS-mode"
#define SPACE_SIZE_UNIT_S                       10803
#define SPACE_SIZE_UNIT_L                       "size-unit"
#define SPACE_SUMMARIZE_S                       's'
#define SPACE_SUMMARIZE_L                       "summarize"
#define SPACE_PURPOSE_S                         'p'
#define SPACE_PURPOSE_L                         "purpose"

/* lockdb option list */
#define LOCK_OUTPUT_FILE_S                      'o'
#define LOCK_OUTPUT_FILE_L                      "output-file"

/* optimizedb option list */
#define OPTIMIZE_CLASS_NAME_S                   'n'
#define OPTIMIZE_CLASS_NAME_L                   "class-name"

/* installdb option list */
#define INSTALL_SERVER_NAME_S                   11100
#define INSTALL_SERVER_NAME_L                   "server-name"
#define INSTALL_FILE_PATH_S                     'F'
#define INSTALL_FILE_PATH_L                     "file-path"
#define INSTALL_LOG_PATH_S                      'L'
#define INSTALL_LOG_PATH_L                      "log-path"

/* diagdb option list */
#define DIAG_DUMP_TYPE_S                        'd'
#define DIAG_DUMP_TYPE_L                        "dump-type"
#define DIAG_DUMP_RECORDS_S                     11201
#define DIAG_DUMP_RECORDS_L                     "dump-records"
#define DIAG_OUTPUT_FILE_S                      'o'
#define DIAG_OUTPUT_FILE_L                      "output-file"
#define DIAG_EMERGENCY_S                        11202
#define DIAG_EMERGENCY_L                        "emergency"

/* patch option list */
#define PATCH_RECREATE_LOG_S                    'r'
#define PATCH_RECREATE_LOG_L                    "recreate-log"

/* alterdbhost option list */
#define ALTERDBHOST_HOST_S                      'h'
#define ALTERDBHOST_HOST_L                      "host"

/* checkdb option list */
#define CHECK_SA_MODE_S                         'S'
#define CHECK_SA_MODE_L                         "SA-mode"
#define CHECK_CS_MODE_S                         'C'
#define CHECK_CS_MODE_L                         "CS-mode"
#define CHECK_REPAIR_S                          'r'
#define CHECK_REPAIR_L                          "repair"
#define CHECK_INPUT_FILE_S                      'i'
#define CHECK_INPUT_FILE_L                      "input-file"
#define CHECK_INDEXNAME_S                       'I'
#define CHECK_INDEXNAME_L                       "index-name"
#define CHECK_CHECK_PREV_LINK_S                 11501
#define CHECK_CHECK_PREV_LINK_L                 "check-prev-link"
#define CHECK_REPAIR_PREV_LINK_S                11502
#define CHECK_REPAIR_PREV_LINK_L                "repair-prev-link"
#define CHECK_FILE_TRACKER_S                    11503
#define CHECK_FILE_TRACKER_L                    "check-file-tracker"
#define CHECK_HEAP_ALLHEAPS_S                   11504
#define CHECK_HEAP_ALLHEAPS_L                   "check-heap"
#define CHECK_CAT_CONSISTENCY_S                 11505
#define CHECK_CAT_CONSISTENCY_L                 "check-catalog"
#define CHECK_BTREE_ALL_BTREES_S                11506
#define CHECK_BTREE_ALL_BTREES_L                "check-btree"
#define CHECK_LC_CLASSNAMES_S                   11507
#define CHECK_LC_CLASSNAMES_L                   "check-class-name"
#define CHECK_LC_ALLENTRIES_OF_ALLBTREES_S      11508
#define CHECK_LC_ALLENTRIES_OF_ALLBTREES_L      "check-btree-entries"

/* plandump option list */
#define PLANDUMP_DROP_S			        'd'
#define PLANDUMP_DROP_L                         "drop"
#define PLANDUMP_OUTPUT_FILE_S		        'o'
#define PLANDUMP_OUTPUT_FILE_L                  "output-file"

/* tranlist option list */
#if defined(NEED_PRIVILEGE_PASSWORD)
#define TRANLIST_USER_S                         'u'
#define TRANLIST_USER_L                         "user"
#define TRANLIST_PASSWORD_S                     'p'
#define TRANLIST_PASSWORD_L                     "password"
#endif
#define TRANLIST_SUMMARY_S                      's'
#define TRANLIST_SUMMARY_L                      "summary"
#define TRANLIST_SORT_KEY_S                     'k'
#define TRANLIST_SORT_KEY_L                     "sort-key"
#define TRANLIST_REVERSE_S                      'r'
#define TRANLIST_REVERSE_L                      "reverse"
#define TRANLIST_FULL_SQL_S                     'f'
#define TRANLIST_FULL_SQL_L                     "full"


/* killtran option list */
#define KILLTRAN_KILL_TRANSACTION_INDEX_S       'i'
#define KILLTRAN_KILL_TRANSACTION_INDEX_L       "kill-transaction-index"
#define KILLTRAN_KILL_USER_NAME_S               11701
#define KILLTRAN_KILL_USER_NAME_L               "kill-user-name"
#define KILLTRAN_KILL_HOST_NAME_S               11702
#define KILLTRAN_KILL_HOST_NAME_L               "kill-host-name"
#define KILLTRAN_KILL_PROGRAM_NAME_S            11703
#define KILLTRAN_KILL_PROGRAM_NAME_L            "kill-program-name"
#define KILLTRAN_KILL_SQL_ID_S                  11704
#define KILLTRAN_KILL_SQL_ID_L                  "kill-sql-id"
#define KILLTRAN_DBA_PASSWORD_S                 'p'
#define KILLTRAN_DBA_PASSWORD_L                 "dba-password"
#define KILLTRAN_DISPLAY_INFORMATION_S          'd'
#define KILLTRAN_DISPLAY_INFORMATION_L          "display-information"
#define KILLTRAN_DISPLAY_QUERY_INFO_S           'q'
#define KILLTRAN_DISPLAY_QUERY_INFO_L           "query-exec-info"
#define KILLTRAN_FORCE_S                        'f'
#define KILLTRAN_FORCE_L                        "force"

/* loaddb option list */
#define LOAD_USER_S                             'u'
#define LOAD_USER_L                             "user"
#define LOAD_PASSWORD_S                         'p'
#define LOAD_PASSWORD_L                         "password"
#define LOAD_CHECK_ONLY_S                       11803
#define LOAD_CHECK_ONLY_L                       "data-file-check-only"
#define LOAD_LOAD_ONLY_S                        'l'
#define LOAD_LOAD_ONLY_L                        "load-only"
#define LOAD_ESTIMATED_SIZE_S                   11805
#define LOAD_ESTIMATED_SIZE_L                   "estimated-size"
#define LOAD_VERBOSE_S                          'v'
#define LOAD_VERBOSE_L                          "verbose"
#define LOAD_NO_STATISTICS_S                    11807
#define LOAD_NO_STATISTICS_L                    "no-statistics"
#define LOAD_PERIODIC_COMMIT_S                  'c'
#define LOAD_PERIODIC_COMMIT_L                  "periodic-commit"
#define LOAD_NO_OID_S                           11809
#define LOAD_NO_OID_L                           "no-oid"
#define LOAD_SCHEMA_FILE_S                      's'
#define LOAD_SCHEMA_FILE_L                      "schema-file"
#define LOAD_INDEX_FILE_S                       'i'
#define LOAD_INDEX_FILE_L                       "index-file"
#define LOAD_IGNORE_LOGGING_S                   11812
#define LOAD_IGNORE_LOGGING_L                   "no-logging"
#define LOAD_DATA_FILE_S                        'd'
#define LOAD_DATA_FILE_L                        "data-file"
#define LOAD_TRIGGER_FILE_S                     11813
#define LOAD_TRIGGER_FILE_L                     "trigger-file"
#define LOAD_ERROR_CONTROL_FILE_S               'e'
#define LOAD_ERROR_CONTROL_FILE_L               "error-control-file"
#define LOAD_IGNORE_CLASS_S                     11816
#define LOAD_IGNORE_CLASS_L                     "ignore-class-file"
#define LOAD_SA_MODE_S                          'S'
#define LOAD_SA_MODE_L                          "SA-mode"
#define LOAD_CS_MODE_S                          'C'
#define LOAD_CS_MODE_L                          "CS-mode"
#define LOAD_TABLE_NAME_S                       't'
#define LOAD_TABLE_NAME_L                       "table"
#define LOAD_COMPARE_STORAGE_ORDER_S            11820
#define LOAD_COMPARE_STORAGE_ORDER_L            "compare-storage-order"
#define LOAD_CS_FORCE_LOAD_S                    11824
#define LOAD_CS_FORCE_LOAD_L                    "force-load"

/* unloaddb option list */
#define UNLOAD_INPUT_CLASS_FILE_S               'i'
#define UNLOAD_INPUT_CLASS_FILE_L               "input-class-file"
#define UNLOAD_INCLUDE_REFERENCE_S              11901
#define UNLOAD_INCLUDE_REFERENCE_L              "include-reference"
#define UNLOAD_INPUT_CLASS_ONLY_S               11902
#define UNLOAD_INPUT_CLASS_ONLY_L               "input-class-only"
#define UNLOAD_LO_COUNT_S                       11903
#define UNLOAD_LO_COUNT_L                       "lo-count"
#define UNLOAD_ESTIMATED_SIZE_S                 11904
#define UNLOAD_ESTIMATED_SIZE_L                 "estimated-size"
#define UNLOAD_CACHED_PAGES_S                   11905
#define UNLOAD_CACHED_PAGES_L                   "cached-pages"
#define UNLOAD_OUTPUT_PATH_S                    'O'
#define UNLOAD_OUTPUT_PATH_L                    "output-path"
#define UNLOAD_SCHEMA_ONLY_S                    's'
#define UNLOAD_SCHEMA_ONLY_L                    "schema-only"
#define UNLOAD_DATA_ONLY_S                      'd'
#define UNLOAD_DATA_ONLY_L                      "data-only"
#define UNLOAD_OUTPUT_PREFIX_S                  11909
#define UNLOAD_OUTPUT_PREFIX_L                  "output-prefix"
#define UNLOAD_HASH_FILE_S                      11910
#define UNLOAD_HASH_FILE_L                      "hash-file"
#define UNLOAD_VERBOSE_S                        'v'
#define UNLOAD_VERBOSE_L                        "verbose"
#define UNLOAD_USE_DELIMITER_S                  11912
#define UNLOAD_USE_DELIMITER_L                  "use-delimiter"
#define UNLOAD_SA_MODE_S                        'S'
#define UNLOAD_SA_MODE_L                        "SA-mode"
#define UNLOAD_CS_MODE_S                        'C'
#define UNLOAD_CS_MODE_L                        "CS-mode"
#define UNLOAD_DATAFILE_PER_CLASS_S             11915
#define UNLOAD_DATAFILE_PER_CLASS_L             "datafile-per-class"
#define UNLOAD_USER_S                           'u'
#define UNLOAD_USER_L                           "user"
#define UNLOAD_PASSWORD_S                       'p'
#define UNLOAD_PASSWORD_L                       "password"
#define UNLOAD_KEEP_STORAGE_ORDER_S		11918
#define UNLOAD_KEEP_STORAGE_ORDER_L		"keep-storage-order"

/* compactdb option list */
#define COMPACT_VERBOSE_S                       'v'
#define COMPACT_VERBOSE_L                       "verbose"
#define COMPACT_INPUT_CLASS_FILE_S              'i'
#define COMPACT_INPUT_CLASS_FILE_L              "input-class-file"
#define COMPACT_CS_MODE_S			'C'
#define COMPACT_CS_MODE_L			"CS-mode"
#define COMPACT_SA_MODE_S			'S'
#define COMPACT_SA_MODE_L			"SA-mode"
#define COMPACT_PAGES_COMMITED_ONCE_S		'p'
#define COMPACT_PAGES_COMMITED_ONCE_L		"pages-commited-once"
#define COMPACT_DELETE_OLD_REPR_S		'd'
#define COMPACT_DELETE_OLD_REPR_L		"delete-old-repr"
#define COMPACT_INSTANCE_LOCK_TIMEOUT_S		'I'
#define COMPACT_INSTANCE_LOCK_TIMEOUT_L		"Instance-lock-timeout"
#define COMPACT_CLASS_LOCK_TIMEOUT_S		'c'
#define COMPACT_CLASS_LOCK_TIMEOUT_L		"class-lock-timeout"
#define COMPACT_STANDBY_CS_MODE_S               12000
#define COMPACT_STANDBY_CS_MODE_L               "standby"

/* sqlx option list */
#define CSQL_SA_MODE_S                          'S'
#define CSQL_SA_MODE_L                          "SA-mode"
#define CSQL_CS_MODE_S                          'C'
#define CSQL_CS_MODE_L                          "CS-mode"
#define CSQL_USER_S                             'u'
#define CSQL_USER_L                             "user"
#define CSQL_PASSWORD_S                         'p'
#define CSQL_PASSWORD_L                         "password"
#define CSQL_ERROR_CONTINUE_S                   'e'
#define CSQL_ERROR_CONTINUE_L                   "error-continue"
#define CSQL_INPUT_FILE_S                       'i'
#define CSQL_INPUT_FILE_L                       "input-file"
#define CSQL_OUTPUT_FILE_S                      'o'
#define CSQL_OUTPUT_FILE_L                      "output-file"
#define CSQL_SINGLE_LINE_S                      's'
#define CSQL_SINGLE_LINE_L                      "single-line"
#define CSQL_COMMAND_S                          'c'
#define CSQL_COMMAND_L                          "command"
#define CSQL_LINE_OUTPUT_S                      'l'
#define CSQL_LINE_OUTPUT_L                      "line-output"
#define CSQL_READ_ONLY_S                        'r'
#define CSQL_READ_ONLY_L                        "read-only"
#define CSQL_NO_AUTO_COMMIT_S                   12010
#define CSQL_NO_AUTO_COMMIT_L                   "no-auto-commit"
#define CSQL_NO_PAGER_S                         12011
#define CSQL_NO_PAGER_L                         "no-pager"
#define CSQL_SYSADM_S                           12012
#define CSQL_SYSADM_L                           "sysadm"
#define CSQL_NO_SINGLE_LINE_S                   12013
#define CSQL_NO_SINGLE_LINE_L                   "no-single-line"
#define CSQL_STRING_WIDTH_S                     12014
#define CSQL_STRING_WIDTH_L                     "string-width"
#define CSQL_WRITE_ON_STANDBY_S                 12015
#define CSQL_WRITE_ON_STANDBY_L                 "write-on-standby"
#define CSQL_NO_TRIGGER_ACTION_S                12016
#define CSQL_NO_TRIGGER_ACTION_L                "no-trigger-action"
#define CSQL_PLAIN_OUTPUT_S                     't'
#define CSQL_PLAIN_OUTPUT_L                     "plain-output"
#define CSQL_SKIP_COL_NAMES_S                   'N'
#define CSQL_SKIP_COL_NAMES_L                   "skip-column-names"
#define CSQL_SKIP_VACUUM_S			12017
#define CSQL_SKIP_VACUUM_L			"skip-vacuum"
#define CSQL_QUERY_OUTPUT_S			'q'
#define CSQL_QUERY_OUTPUT_L			"query-output"
#define CSQL_QUERY_COLUMN_DELIMITER_S		12018
#define CSQL_QUERY_COLUMN_DELIMITER_L		"delimiter"
#define CSQL_QUERY_COLUMN_ENCLOSURE_S		12019
#define CSQL_QUERY_COLUMN_ENCLOSURE_L		"enclosure"
#define CSQL_LOADDB_OUTPUT_S			'd'
#define CSQL_LOADDB_OUTPUT_L			"loaddb-output"

#define COMMDB_SERVER_LIST_S                    'P'
#define COMMDB_SERVER_LIST_L                    "server-list"
#define COMMDB_ALL_LIST_S                       'O'
#define COMMDB_ALL_LIST_L                       "all-list"
#define COMMDB_SHUTDOWN_SERVER_S                'S'
#define COMMDB_SHUTDOWN_SERVER_L                "shutdown-server"
#define COMMDB_SHUTDOWN_ALL_S                   'A'
#define COMMDB_SHUTDOWN_ALL_L                   "shutdown-all"
#define COMMDB_HOST_S                           'h'
#define COMMDB_HOST_L                           "host"
#define COMMDB_SERVER_MODE_S                    'c'
#define COMMDB_SERVER_MODE_L                    "server-mode"
#define COMMDB_HA_NODE_LIST_S                   'N'
#define COMMDB_HA_NODE_LIST_L                   "node-list"
#define COMMDB_HA_PROCESS_LIST_S                'L'
#define COMMDB_HA_PROCESS_LIST_L                "process-list"
#define COMMDB_HA_PING_HOST_LIST_S              'p'
#define COMMDB_HA_PING_HOST_LIST_L              "ping-host"
#define COMMDB_DEREG_HA_BY_PID_S                'D'
#define COMMDB_DEREG_HA_BY_PID_L                "dereg-process"
#define COMMDB_DEREG_HA_BY_ARGS_S               'R'
#define COMMDB_DEREG_HA_BY_ARGS_L               "dereg-args"
#define COMMDB_KILL_ALL_HA_PROCESS_S            'd'
#define COMMDB_KILL_ALL_HA_PROCESS_L            "kill-all-ha-process"
#define COMMDB_IS_REGISTERED_PROC_S             'C'
#define COMMDB_IS_REGISTERED_PROC_L             "is-registered-proc"
#define COMMDB_RECONFIG_HEARTBEAT_S             'F'
#define COMMDB_RECONFIG_HEARTBEAT_L             "reconfig-node-list"
#define COMMDB_DEACTIVATE_HEARTBEAT_S           12110
#define COMMDB_DEACTIVATE_HEARTBEAT_L           "deactivate-heartbeat"
#define COMMDB_DEACT_STOP_ALL_S                 12111
#define COMMDB_DEACT_STOP_ALL_L                 "deact-stop-all"
#define COMMDB_DEACT_CONFIRM_STOP_ALL_S         12112
#define COMMDB_DEACT_CONFIRM_STOP_ALL_L         "deact-confirm-stop-all"
#define COMMDB_DEACT_CONFIRM_NO_SERVER_S        12113
#define COMMDB_DEACT_CONFIRM_NO_SERVER_L        "deact-confirm-no-server"
#define COMMDB_ACTIVATE_HEARTBEAT_S             12114
#define COMMDB_ACTIVATE_HEARTBEAT_L             "activate-heartbeat"
#define COMMDB_VERBOSE_OUTPUT_S                 'V'
#define COMMDB_VERBOSE_OUTPUT_L	                "verbose"
#define COMMDB_HB_DEACT_IMMEDIATELY_S           'i'
#define COMMDB_HB_DEACT_IMMEDIATELY_L           "immediately"
#define COMMDB_HA_ADMIN_INFO_S                  12115
#define COMMDB_HA_ADMIN_INFO_L                  "admin-info"
#define COMMDB_HA_START_UTIL_PROCESS_S          't'
#define COMMDB_HA_START_UTIL_PROCESS_L          "start-ha-util-process"

/* paramdump option list */
#define PARAMDUMP_OUTPUT_FILE_S                 'o'
#define PARAMDUMP_OUTPUT_FILE_L                 "output-file"
#define PARAMDUMP_BOTH_S                        'b'
#define PARAMDUMP_BOTH_L                        "both"
#define PARAMDUMP_SA_MODE_S                     'S'
#define PARAMDUMP_SA_MODE_L                     "SA-mode"
#define PARAMDUMP_CS_MODE_S                     'C'
#define PARAMDUMP_CS_MODE_L                     "CS-mode"

/* statdump option list */
#define STATDUMP_OUTPUT_FILE_S                  'o'
#define STATDUMP_OUTPUT_FILE_L                  "output-file"
#define STATDUMP_INTERVAL_S                     'i'
#define STATDUMP_INTERVAL_L                     "interval"
#define STATDUMP_CUMULATIVE_S                   'c'
#define STATDUMP_CUMULATIVE_L                   "cumulative"
#define STATDUMP_SUBSTR_S			's'
#define STATDUMP_SUBSTR_L			"substr"

/* acl option list */
#define ACLDB_RELOAD_S                          'r'
#define ACLDB_RELOAD_L				"reload"

/* changemode option list */
#define CHANGEMODE_MODE_S                       'm'
#define CHANGEMODE_MODE_L                       "mode"
#define CHANGEMODE_FORCE_S                      'f'
#define CHANGEMODE_FORCE_L                      "force"
#define CHANGEMODE_TIMEOUT_S			't'
#define CHANGEMODE_TIMEOUT_L			"timeout"

/* copylogdb option list */
#define COPYLOG_LOG_PATH_S                      'L'
#define COPYLOG_LOG_PATH_L                      "log-path"
#define COPYLOG_MODE_S                          'm'
#define COPYLOG_MODE_L                          "mode"
#define COPYLOG_START_PAGEID_S			'S'
#define COPYLOG_START_PAGEID_L			"start-page-id"

/* applylogdb option list */
#define APPLYLOG_LOG_PATH_S                     'L'
#define APPLYLOG_LOG_PATH_L                     "log-path"
#define APPLYLOG_MAX_MEM_SIZE_S			12401
#define APPLYLOG_MAX_MEM_SIZE_L			"max-mem-size"

/* applyinfo option list */
#define APPLYINFO_COPIED_LOG_PATH_S             'L'
#define APPLYINFO_COPIED_LOG_PATH_L             "copied-log-path"
#define APPLYINFO_PAGE_S                        'p'
#define APPLYINFO_PAGE_L                        "page"
#define APPLYINFO_REMOTE_NAME_S                 'r'
#define APPLYINFO_REMOTE_NAME_L                 "remote-host-name"
#define APPLYINFO_APPLIED_INFO_S		'a'
#define APPLYINFO_APPLIED_INFO_L                "applied-info"
#define APPLYINFO_VERBOSE_S                     'v'
#define APPLYINFO_VERBOSE_L                     "verbose"
#define APPLYINFO_INTERVAL_S                    'i'
#define APPLYINFO_INTERVAL_L                    "interval"

/* genlocale option list */
#define GENLOCALE_INPUT_PATH_S			'i'
#define GENLOCALE_INPUT_PATH_L			"input-ldml-file"
#define GENLOCALE_VERBOSE_S                     'v'
#define GENLOCALE_VERBOSE_L                     "verbose"

/* dumplocale option list */
#define DUMPLOCALE_INPUT_PATH_S			'i'
#define DUMPLOCALE_INPUT_PATH_L			"input-file"
#define DUMPLOCALE_CALENDAR_S                   'd'
#define DUMPLOCALE_CALENDAR_L			"calendar"
#define DUMPLOCALE_NUMBERING_S                  'n'
#define DUMPLOCALE_NUMBERING_L                  "numbering"
#define DUMPLOCALE_ALPHABET_S                   'a'
#define DUMPLOCALE_ALPHABET_L                   "alphabet"
#define DUMPLOCALE_ALPHABET_LOWER_S		"l"
#define DUMPLOCALE_ALPHABET_LOWER_L		"lower"
#define DUMPLOCALE_ALPHABET_UPPER_S		"u"
#define DUMPLOCALE_ALPHABET_UPPER_L		"upper"
#define DUMPLOCALE_ALPHABET_ALL_CASING		"both"
#define DUMPLOCALE_IDENTIFIER_ALPHABET_S        13000
#define DUMPLOCALE_IDENTIFIER_ALPHABET_L        "identifier-alphabet"
#define DUMPLOCALE_COLLATION_S			'c'
#define DUMPLOCALE_COLLATION_L                  "codepoint-order"
#define DUMPLOCALE_WEIGHT_ORDER_S               'w'
#define DUMPLOCALE_WEIGHT_ORDER_L		"weight-order"
#define DUMPLOCALE_START_VALUE_S                's'
#define DUMPLOCALE_START_VALUE_L                "start-value"
#define DUMPLOCALE_END_VALUE_S			'e'
#define DUMPLOCALE_END_VALUE_L			"end-value"
#define DUMPLOCALE_NORMALIZATION_S		'z'
#define DUMPLOCALE_NORMALIZATION_L		"normalization"
#define DUMPLOCALE_CONSOLE_CONV_S		'k'
#define DUMPLOCALE_CONSOLE_CONV_L		"console-conversion"

/* sync_collations option list */
#define SYNCCOLL_CHECK_S			'c'
#define SYNCCOLL_CHECK_L			"check-only"
#define SYNCCOLL_FORCESYNC_S			'f'
#define SYNCCOLL_FORCESYNC_L                    "force-only"

/* gen_tz option list */
#define GEN_TZ_INPUT_FOLDER_S			'i'
#define GEN_TZ_INPUT_FOLDER_L			"input-folder"
#define GEN_TZ_MODE_S				'g'
#define GEN_TZ_MODE_L				"gen-mode"

/* dump_tz option list */
#define DUMP_TZ_COUNTRIES_S			'c'
#define DUMP_TZ_COUNTRIES_L			"list-countries"
#define DUMP_TZ_ZONES_S				'z'
#define DUMP_TZ_ZONES_L				"list-zones"
#define DUMP_TZ_ZONE_ID_S			'd'
#define DUMP_TZ_ZONE_ID_L			"zone-id"
#define DUMP_TZ_LEAP_SEC_S			'l'
#define DUMP_TZ_LEAP_SEC_L			"leap-seconds"
#define DUMP_TZ_DUMP_SUM_S			's'
#define DUMP_TZ_DUMP_SUM_L			"summary"

#define VERSION_S                               20000
#define VERSION_L                               "version"

/* restoreslave option list */
#define RESTORESLAVE_SOURCE_STATE_S                  's'
#define RESTORESLAVE_SOURCE_STATE_L                  "source-state"
#define RESTORESLAVE_MASTER_HOST_NAME_S              'm'
#define RESTORESLAVE_MASTER_HOST_NAME_L              "master-host-name"
#define RESTORESLAVE_LIST_S                          10601
#define RESTORESLAVE_LIST_L                          "list"
#define RESTORESLAVE_BACKUP_FILE_PATH_S              'B'
#define RESTORESLAVE_BACKUP_FILE_PATH_L              "backup-file-path"
#define RESTORESLAVE_OUTPUT_FILE_S                   'o'
#define RESTORESLAVE_OUTPUT_FILE_L                   "output-file"
#define RESTORESLAVE_USE_DATABASE_LOCATION_PATH_S    'u'
#define RESTORESLAVE_USE_DATABASE_LOCATION_PATH_L    "use-database-location-path"
#define RESTORESLAVE_KEYS_FILE_PATH_S                'k'
#define RESTORESLAVE_KEYS_FILE_PATH_L                "keys-file-path"

/* vacuumdb option list */
#define VACUUM_SA_MODE_S                         'S'
#define VACUUM_SA_MODE_L                         "SA-mode"
#define VACUUM_CS_MODE_S                         'C'
#define VACUUM_CS_MODE_L                         "CS-mode"
#define VACUUM_DUMP_S                            10700
#define VACUUM_DUMP_L                            "dump"
#define VACUUM_OUTPUT_FILE_S                     'o'
#define VACUUM_OUTPUT_FILE_L                     "output-file"

/* checksumdb option list */
#define CHECKSUM_CHUNK_SIZE_S			'c'
#define CHECKSUM_CHUNK_SIZE_L			"chunk-size"
#define CHECKSUM_RESUME_S			14000
#define CHECKSUM_RESUME_L			"resume"
#define CHECKSUM_SLEEP_S			's'
#define CHECKSUM_SLEEP_L			"sleep"
#define CHECKSUM_CONT_ON_ERROR_S		14001
#define CHECKSUM_CONT_ON_ERROR_L		"cont-on-error"
#define CHECKSUM_INCLUDE_CLASS_FILE_S		'i'
#define CHECKSUM_INCLUDE_CLASS_FILE_L		"include-class-file"
#define CHECKSUM_EXCLUDE_CLASS_FILE_S		'e'
#define CHECKSUM_EXCLUDE_CLASS_FILE_L		"exclude-class-file"
#define CHECKSUM_TIMEOUT_S			't'
#define CHECKSUM_TIMEOUT_L			"timeout"
#define CHECKSUM_TABLE_NAME_S			'n'
#define CHECKSUM_TABLE_NAME_L			"table-name"
#define CHECKSUM_REPORT_ONLY_S			'r'
#define CHECKSUM_REPORT_ONLY_L			"report-only"
#define CHECKSUM_SCHEMA_ONLY_S			14002
#define CHECKSUM_SCHEMA_ONLY_L			"schema-only"

/* tde option list */
#define TDE_GENERATE_KEY_S    'n'
#define TDE_GENERATE_KEY_L    "generate-new-key"
#define TDE_SHOW_KEYS_S       's'
#define TDE_SHOW_KEYS_L       "show-keys"
#define TDE_PRINT_KEY_VALUE_S 14000
#define TDE_PRINT_KEY_VALUE_L "print-value"
#define TDE_SA_MODE_S         'S'
#define TDE_SA_MODE_L         "SA-mode"
#define TDE_CS_MODE_S         HIDDEN_CS_MODE_S
#define TDE_CS_MODE_L         "CS-mode"
#define TDE_CHANGE_KEY_S      'c'
#define TDE_CHANGE_KEY_L      "change-key"
#define TDE_DELETE_KEY_S      'd'
#define TDE_DELETE_KEY_L      "delete-key"
#define TDE_DBA_PASSWORD_S    'p'
#define TDE_DBA_PASSWORD_L    "dba-password"

#if defined(WINDOWS)
#define LIB_UTIL_CS_NAME                "cubridcs.dll"
#define LIB_UTIL_SA_NAME                "cubridsa.dll"
#elif defined(_AIX)
#define makestring1(x) #x
#define makestring(x) makestring1(x)

#define LIB_UTIL_CS_NAME                \
  "libcubridcs.a(libcubridcs.so." makestring(MAJOR_VERSION) ")"
#define LIB_UTIL_SA_NAME                \
  "libcubridsa.a(libcubridsa.so." makestring(MAJOR_VERSION) ")"
#else
#define LIB_UTIL_CS_NAME                "libcubridcs.so"
#define LIB_UTIL_SA_NAME                "libcubridsa.so"
#endif

#define UTILITY_GENERIC_MSG_FUNC_NAME	"utility_get_generic_message"
#define UTILITY_INIT_FUNC_NAME	        "utility_initialize"
#define UTILITY_ADMIN_USAGE_FUNC_NAME   "util_admin_usage"
#define UTILITY_ADMIN_VERSION_FUNC_NAME "util_admin_version"
typedef int (*UTILITY_INIT_FUNC) (void);

/* extern functions */
#ifdef __cplusplus
extern "C"
{
#endif
  extern int utility_initialize (void);
  extern const char *utility_get_generic_message (int message_index);
  extern int check_database_name (const char *name);
  extern int check_new_database_name (const char *name);
  extern int check_volume_name (const char *name);
  extern int utility_get_option_int_value (UTIL_ARG_MAP * arg_map, int arg_ch);
  extern bool utility_get_option_bool_value (UTIL_ARG_MAP * arg_map, int arg_ch);
  extern char *utility_get_option_string_value (UTIL_ARG_MAP * arg_map, int arg_ch, int index);
  extern INT64 utility_get_option_bigint_value (UTIL_ARG_MAP * arg_map, int arg_ch);
  extern int utility_get_option_string_table_size (UTIL_ARG_MAP * arg_map);

  extern FILE *fopen_ex (const char *filename, const char *type);

  extern bool util_is_localhost (char *host);
  extern bool are_hostnames_equal (const char *hostname_a, const char *hostname_b);

  extern void util_free_ha_conf (HA_CONF * ha_conf);
  extern int util_make_ha_conf (HA_CONF * ha_conf);
  extern int util_get_ha_mode_for_sa_utils (void);
  extern int util_get_num_of_ha_nodes (const char *node_list);
#if !defined(WINDOWS)
  extern void util_redirect_stdout_to_null (void);
#endif				/* !defined(WINDOWS) */
  extern int util_byte_to_size_string (char *buf, size_t len, UINT64 size_num);
  extern int util_size_string_to_byte (UINT64 * size_num, const char *size_str);
  extern int util_msec_to_time_string (char *buf, size_t len, INT64 msec_num);
  extern int util_time_string_to_msec (INT64 * msec_num, char *time_str);
  extern void util_print_deprecated (const char *option);
  extern int util_get_table_list_from_file (char *fname, dynamic_array * darray);

  typedef struct
  {
    int keyval;
    const char *keystr;
  } UTIL_KEYWORD;

  extern int changemode_keyword (int *keyval_p, char **keystr_p);
  extern int copylogdb_keyword (int *keyval_p, char **keystr_p);

  extern int utility_keyword_value (UTIL_KEYWORD * keywords, int *keyval_p, char **keystr_p);
  extern int utility_keyword_search (UTIL_KEYWORD * keywords, int *keyval_p, char **keystr_p);

  extern int utility_localtime (const time_t * ts, struct tm *result);

/* admin utility main functions */
  typedef struct
  {
    UTIL_ARG_MAP *arg_map;
    const char *command_name;
    char *argv0;
    char **argv;
    bool valid_arg;
  } UTIL_FUNCTION_ARG;
  typedef int (*UTILITY_FUNCTION) (UTIL_FUNCTION_ARG *);

  extern int compactdb (UTIL_FUNCTION_ARG * arg_map);
#if defined (ENABLE_UNUSED_FUNCTION)
  extern int loaddb_dba (UTIL_FUNCTION_ARG * arg_map);
#endif
  extern int loaddb_user (UTIL_FUNCTION_ARG * arg_map);
  extern int unloaddb (UTIL_FUNCTION_ARG * arg_map);
  extern int backupdb (UTIL_FUNCTION_ARG * arg_map);
  extern int addvoldb (UTIL_FUNCTION_ARG * arg_map);
#if 0
  extern int delvoldb (UTIL_FUNCTION_ARG * arg_map);
#endif
  extern int checkdb (UTIL_FUNCTION_ARG * arg_map);
  extern int spacedb (UTIL_FUNCTION_ARG * arg_map);
  extern int lockdb (UTIL_FUNCTION_ARG * arg_map);
  extern int tranlist (UTIL_FUNCTION_ARG * arg_map);
  extern int killtran (UTIL_FUNCTION_ARG * arg_map);
  extern int restartevnt (UTIL_FUNCTION_ARG * arg_map);
  extern int prestartldb (UTIL_FUNCTION_ARG * arg_map);
  extern int shutdownldb (UTIL_FUNCTION_ARG * arg_map);
  extern int mqueueldb (UTIL_FUNCTION_ARG * arg_map);
  extern int plandump (UTIL_FUNCTION_ARG * arg_map);
  extern int createdb (UTIL_FUNCTION_ARG * arg_map);
  extern int deletedb (UTIL_FUNCTION_ARG * arg_map);
  extern int restoredb (UTIL_FUNCTION_ARG * arg_map);
  extern int renamedb (UTIL_FUNCTION_ARG * arg_map);
  extern int installdb (UTIL_FUNCTION_ARG * arg_map);
  extern int copydb (UTIL_FUNCTION_ARG * arg_map);
  extern int optimizedb (UTIL_FUNCTION_ARG * arg_map);
  extern int diagdb (UTIL_FUNCTION_ARG * arg_map);
  extern int patchdb (UTIL_FUNCTION_ARG * arg_map);
  extern int estimatedb_data (UTIL_FUNCTION_ARG * arg_map);
  extern int estimatedb_index (UTIL_FUNCTION_ARG * arg_map);
  extern int estimatedb_hash (UTIL_FUNCTION_ARG * arg_map);
  extern int alterdbhost (UTIL_FUNCTION_ARG * arg_map);
  extern int paramdump (UTIL_FUNCTION_ARG * arg_map);
  extern int statdump (UTIL_FUNCTION_ARG * arg_map);
  extern int changemode (UTIL_FUNCTION_ARG * arg_map);
  extern int copylogdb (UTIL_FUNCTION_ARG * arg_map);
  extern int applylogdb (UTIL_FUNCTION_ARG * arg_map);
  extern int applyinfo (UTIL_FUNCTION_ARG * arg_map);
  extern int acldb (UTIL_FUNCTION_ARG * arg_map);
  extern int genlocale (UTIL_FUNCTION_ARG * arg_map);
  extern int dumplocale (UTIL_FUNCTION_ARG * arg_map);
  extern int synccolldb (UTIL_FUNCTION_ARG * arg_map);
  extern int gen_tz (UTIL_FUNCTION_ARG * arg_map);
  extern int dump_tz (UTIL_FUNCTION_ARG * arg_map);
  extern int synccoll_force (void);
  extern int restoreslave (UTIL_FUNCTION_ARG * arg_map);
  extern int vacuumdb (UTIL_FUNCTION_ARG * arg_map);
  extern int checksumdb (UTIL_FUNCTION_ARG * arg_map);
  extern int tde (UTIL_FUNCTION_ARG * arg_map);

  extern void util_admin_usage (const char *argv0);
  extern void util_admin_version (const char *argv0);
#ifdef __cplusplus
}
#endif
#endif				/* _UTILITY_H_ */
