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
 * keyword.c - SQL keyword table
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>


#include "csql_grammar.h"
#include "parser.h"
#include "dbtype.h"
#include "string_opfunc.h"
#include "chartype.h"

/* It is not required for the keywords to be alphabetically sorted, as they
 * will be sorted when needed. See pt_find_keyword.
 */

/* Keyword names must be written in uppercase */
static KEYWORD_RECORD keywords[] = {
  {ABSOLUTE_, "ABSOLUTE", 0},
  {ACCESS, "ACCESS", 1},
  {ACTION, "ACTION", 0},
  {ACTIVE, "ACTIVE", 1},
  {ADD, "ADD", 0},
  {ADD_MONTHS, "ADD_MONTHS", 0},
  {ADDDATE, "ADDDATE", 1},
  {AFTER, "AFTER", 0},
  {ALL, "ALL", 0},
  {ALLOCATE, "ALLOCATE", 0},
  {ALTER, "ALTER", 0},
  {ANALYZE, "ANALYZE", 1},
  {AND, "AND", 0},
  {ANY, "ANY", 0},
  {ARCHIVE, "ARCHIVE", 1},
  {ARE, "ARE", 0},
  {AS, "AS", 0},
  {ASC, "ASC", 0},
  {ASSERTION, "ASSERTION", 0},
  {ASYNC, "ASYNC", 0},
  {AT, "AT", 0},
  {ATTACH, "ATTACH", 0},
  {ATTRIBUTE, "ATTRIBUTE", 0},
  {AUTO_INCREMENT, "AUTO_INCREMENT", 1},
  {AVG, "AVG", 0},
  {BEFORE, "BEFORE", 0},
  {BEGIN_, "BEGIN", 0},
  {BETWEEN, "BETWEEN", 0},
  {BIGINT, "BIGINT", 0},
  {BINARY, "BINARY", 0},
  {BIT, "BIT", 0},
  {BIT_AND, "BIT_AND", 1},
  {BIT_LENGTH, "BIT_LENGTH", 0},
  {BIT_OR, "BIT_OR", 1},
  {BIT_XOR, "BIT_XOR", 1},
  {BLOB_, "BLOB", 0},
  {BOOLEAN_, "BOOLEAN", 0},
  {BOTH_, "BOTH", 0},
  {BREADTH, "BREADTH", 0},
  {BY, "BY", 0},
  {BUFFER, "BUFFER", 1},
  {CALL, "CALL", 0},
  {CACHE, "CACHE", 1},
  {CAPACITY, "CAPACITY", 1},
  {CASCADE, "CASCADE", 0},
  {CASCADED, "CASCADED", 0},
  {CASE, "CASE", 0},
  {CAST, "CAST", 0},
  {CATALOG, "CATALOG", 0},
  {CHANGE, "CHANGE", 0},
  {CHAR_, "CHAR", 0},
  {CHARSET, "CHARSET", 1},
  {CHARACTER_SET_, "CHARACTER_SET", 1},
  {CHECK, "CHECK", 0},
  {CHR, "CHR", 1},
  {CLASS, "CLASS", 0},
  {CLASSES, "CLASSES", 0},
  {CLOB_, "CLOB", 0},
  {CLOB_TO_CHAR, "CLOB_TO_CHR", 1},
  {CLOSE, "CLOSE", 1},
  {COALESCE, "COALESCE", 0},
  {COLLATE, "COLLATE", 0},
  {COLLATION, "COLLATION", 1},
  {COLUMN, "COLUMN", 0},
  {COLUMNS, "COLUMNS", 1},
  {COMMENT, "COMMENT", 1},
  {COMMIT, "COMMIT", 0},
  {COMMITTED, "COMMITTED", 1},
  {CONNECT, "CONNECT", 0},
  {CONNECT_BY_ISCYCLE, "CONNECT_BY_ISCYCLE", 0},
  {CONNECT_BY_ISLEAF, "CONNECT_BY_ISLEAF", 0},
  {CONNECT_BY_ROOT, "CONNECT_BY_ROOT", 0},
  {CONNECTION, "CONNECTION", 0},
  {CONSTRAINT, "CONSTRAINT", 0},
  {CONSTRAINTS, "CONSTRAINTS", 0},
  {CONTINUE, "CONTINUE", 0},
  {CONVERT, "CONVERT", 0},
  {CORRESPONDING, "CORRESPONDING", 0},
  {COST, "COST", 1},
  {COUNT, "COUNT", 0},
  {CREATE, "CREATE", 0},
  {CRITICAL, "CRITICAL", 1},
  {CROSS, "CROSS", 0},
  {CUME_DIST, "CUME_DIST", 1},
  {CURRENT, "CURRENT", 0},
  {CURRENT_DATE, "CURRENT_DATE", 0},
  {CURRENT_TIME, "CURRENT_TIME", 0},
  {CURRENT_TIMESTAMP, "CURRENT_TIMESTAMP", 0},
  {CURRENT_DATETIME, "CURRENT_DATETIME", 0},
  {CURRENT_USER, "CURRENT_USER", 0},
  {CURSOR, "CURSOR", 0},
  {CYCLE, "CYCLE", 0},
  {DATA, "DATA", 0},
  {DATA_TYPE___, "DATA_TYPE___", 0},
  {DATABASE, "DATABASE", 0},
  {Date, "DATE", 0},
  {DATE_ADD, "DATE_ADD", 1},
  {DATE_SUB, "DATE_SUB", 1},
  {DAY_, "DAY", 0},
  {DAY_HOUR, "DAY_HOUR", 0},
  {DAY_MILLISECOND, "DAY_MILLISECOND", 0},
  {DAY_MINUTE, "DAY_MINUTE", 0},
  {DAY_SECOND, "DAY_SECOND", 0},
  {DEALLOCATE, "DEALLOCATE", 0},
  {NUMERIC, "DEC", 0},
  {NUMERIC, "DECIMAL", 0},
  {DECLARE, "DECLARE", 0},
  {DECREMENT, "DECREMENT", 1},
  {DEFAULT, "DEFAULT", 0},
  {DEFERRABLE, "DEFERRABLE", 0},
  {DEFERRED, "DEFERRED", 0},
  {DELETE_, "DELETE", 0},
  {DENSE_RANK, "DENSE_RANK", 1},
  {DEPTH, "DEPTH", 0},
  {DESC, "DESC", 0},
  {DESCRIBE, "DESCRIBE", 0},
  {DESCRIPTOR, "DESCRIPTOR", 0},
  {DIAGNOSTICS, "DIAGNOSTICS", 0},
  {DIFFERENCE_, "DIFFERENCE", 0},
  {DISCONNECT, "DISCONNECT", 0},
  {DISTINCT, "DISTINCT", 0},
  {DIV, "DIV", 0},
  {DO, "DO", 0},
  {Domain, "DOMAIN", 0},
  {Double, "DOUBLE", 0},
  {DROP, "DROP", 0},
  {DUPLICATE_, "DUPLICATE", 0},
  {EACH, "EACH", 0},
  {ELSE, "ELSE", 0},
  {ELSEIF, "ELSEIF", 0},
  {ELT, "ELT", 1},
  {END, "END", 0},
  {EQUALS, "EQUALS", 0},
  {ENUM, "ENUM", 0},
  {ESCAPE, "ESCAPE", 0},
  {EVALUATE, "EVALUATE", 0},
  {EXCEPT, "EXCEPT", 0},
  {EXCEPTION, "EXCEPTION", 0},
  {EXEC, "EXEC", 0},
  {EXECUTE, "EXECUTE", 0},
  {EXISTS, "EXISTS", 0},
  {EXPLAIN, "EXPLAIN", 1},
  {EXTERNAL, "EXTERNAL", 0},
  {EXTRACT, "EXTRACT", 0},
  {False, "FALSE", 0},
  {FETCH, "FETCH", 0},
  {File, "FILE", 0},
  {FIRST, "FIRST", 0},
  {FIRST_VALUE, "FIRST_VALUE", 1},
  {FLOAT_, "FLOAT", 0},
  {For, "FOR", 0},
  {FOREIGN, "FOREIGN", 0},
  {FOUND, "FOUND", 0},
  {FROM, "FROM", 0},
  {FULL, "FULL", 0},
  {FULLSCAN, "FULLSCAN", 1},
  {FUNCTION, "FUNCTION", 0},
  {GENERAL, "GENERAL", 0},
  {GET, "GET", 0},
  {GE_INF_, "GE_INF", 1},
  {GE_LE_, "GE_LE", 1},
  {GE_LT_, "GE_LT", 1},
  {GLOBAL, "GLOBAL", 0},
  {GO, "GO", 0},
  {GOTO, "GOTO", 0},
  {GRANT, "GRANT", 0},
  {GRANTS, "GRANTS", 1},
  {GROUP_, "GROUP", 0},
  {GROUPS, "GROUPS", 1},
  {GROUP_CONCAT, "GROUP_CONCAT", 1},
  {GT_INF_, "GT_INF", 1},
  {GT_LE_, "GT_LE", 1},
  {GT_LT_, "GT_LT", 1},
  {HASH, "HASH", 1},
  {HAVING, "HAVING", 0},
  {HEADER, "HEADER", 1},
  {HEAP, "HEAP", 1},
  {HOUR_, "HOUR", 0},
  {HOUR_MINUTE, "HOUR_MINUTE", 0},
  {HOUR_MILLISECOND, "HOUR_MILLISECOND", 0},
  {HOUR_SECOND, "HOUR_SECOND", 0},
  {IDENTITY, "IDENTITY", 0},
  {IF, "IF", 0},
  {IFNULL, "IFNULL", 1},
  {IGNORE_, "IGNORE", 0},
  {IMMEDIATE, "IMMEDIATE", 0},
  {IN_, "IN", 0},
  {INACTIVE, "INACTIVE", 1},
  {INCREMENT, "INCREMENT", 1},
  {INDEX, "INDEX", 0},
  {INDEX_PREFIX, "INDEX_PREFIX", 1},
  {INDEXES, "INDEXES", 1},
  {INDICATOR, "INDICATOR", 0},
  {INFINITE_, "INFINITE", 1},
  {INF_LE_, "INF_LE", 1},
  {INF_LT_, "INF_LT", 1},
  {INHERIT, "INHERIT", 0},
  {INITIALLY, "INITIALLY", 0},
  {INNER, "INNER", 0},
  {INOUT, "INOUT", 0},
  {INPUT_, "INPUT", 0},
  {INSERT, "INSERT", 0},
  {INSTANCES, "INSTANCES", 1},
  {INTEGER, "INT", 0},
  {INTEGER, "INTEGER", 0},
  {INTERSECT, "INTERSECT", 0},
  {INTERSECTION, "INTERSECTION", 0},
  {INTERVAL, "INTERVAL", 0},
  {INTO, "INTO", 0},
  {INVALIDATE, "INVALIDATE", 1},
  {IS, "IS", 0},
  {ISNULL, "ISNULL", 1},
  {ISOLATION, "ISOLATION", 0},
  {JAVA, "JAVA", 1},
  {JOIN, "JOIN", 0},
  {JOB, "JOB", 1},
  {JSON, "JSON", 0},
  {KEY, "KEY", 0},
  {KEYS, "KEYS", 1},
  {KILL, "KILL", 1},
  {LANGUAGE, "LANGUAGE", 0},
  {LAST, "LAST", 0},
  {LAST_VALUE, "LAST_VALUE", 1},
  {LCASE, "LCASE", 1},
  {LEADING_, "LEADING", 0},
  {LEAVE, "LEAVE", 0},
  {LEFT, "LEFT", 0},
  {LESS, "LESS", 0},
  {LEVEL, "LEVEL", 0},
  {LIKE, "LIKE", 0},
  {LIMIT, "LIMIT", 0},
  {LIST, "LIST", 0},
  {LOCAL, "LOCAL", 0},
  {LOCAL_TRANSACTION_ID, "LOCAL_TRANSACTION_ID", 0},
  {LOCALTIME, "LOCALTIME", 0},
  {LOCALTIMESTAMP, "LOCALTIMESTAMP", 0},
  {LOCK_, "LOCK", 1},
  {LOG, "LOG", 1},
  {LOOP, "LOOP", 0},
  {LOWER, "LOWER", 0},
  {MATCH, "MATCH", 0},
  {MATCHED, "MATCHED", 1},
  {Max, "MAX", 0},
  {MAXIMUM, "MAXIMUM", 1},
  {MAXVALUE, "MAXVALUE", 1},
  {MEDIAN, "MEDIAN", 1},
  {MEMBERS, "MEMBERS", 1},
  {MERGE, "MERGE", 0},
  {METHOD, "METHOD", 0},
  {Min, "MIN", 0},
  {MINUTE_, "MINUTE", 0},
  {MINUTE_MILLISECOND, "MINUTE_MILLISECOND", 0},
  {MINUTE_SECOND, "MINUTE_SECOND", 0},
  {MINVALUE, "MINVALUE", 1},
  {MOD, "MOD", 0},
  {MODIFY, "MODIFY", 0},
  {MODULE, "MODULE", 0},
  {Monetary, "MONETARY", 0},
  {MONTH_, "MONTH", 0},
  {MULTISET, "MULTISET", 0},
  {MULTISET_OF, "MULTISET_OF", 0},
  {NA, "NA", 0},
  {NAME, "NAME", 1},
  {NAMES, "NAMES", 0},
  {NATIONAL, "NATIONAL", 0},
  {NATURAL, "NATURAL", 0},
  {NCHAR, "NCHAR", 0},
  {NEXT, "NEXT", 0},
  {NO, "NO", 0},
  {NOCACHE, "NOCACHE", 1},
  {NOCYCLE, "NOCYCLE", 1},
  {NOMAXVALUE, "NOMAXVALUE", 1},
  {NOMINVALUE, "NOMINVALUE", 1},
  {NONE, "NONE", 0},
  {NOT, "NOT", 0},
  {NTH_VALUE, "NTH_VALUE", 1},
  {NTILE, "NTILE", 1},
  {Null, "NULL", 0},
  {NULLIF, "NULLIF", 0},
  {NULLS, "NULLS", 1},
  {NUMERIC, "NUMERIC", 0},
  {OBJECT, "OBJECT", 0},
  {OCTET_LENGTH, "OCTET_LENGTH", 0},
  {OF, "OF", 0},
  {OFFSET, "OFFSET", 1},
  {OFF_, "OFF", 0},
  {ON_, "ON", 0},
  {ONLY, "ONLY", 0},
  {OPEN, "OPEN", 1},
  {OPTIMIZATION, "OPTIMIZATION", 0},
  {OPTION, "OPTION", 0},
  {OR, "OR", 0},
  {ORDER, "ORDER", 0},
  {OUT_, "OUT", 0},
  {OUTER, "OUTER", 0},
  {OUTPUT, "OUTPUT", 0},
  {OVERLAPS, "OVERLAPS", 0},
  {OWNER, "OWNER", 1},
  {PAGE, "PAGE", 1},
  {PARAMETERS, "PARAMETERS", 0},
  {PARTIAL, "PARTIAL", 0},
  {PARTITION, "PARTITION", 0},
  {PARTITIONING, "PARTITIONING", 1},
  {PARTITIONS, "PARTITIONS", 1},
  {PATH, "PATH", 1},
  {PASSWORD, "PASSWORD", 1},
  {PERCENT_RANK, "PERCENT_RANK", 1},
  {PERCENTILE_CONT, "PERCENTILE_CONT", 1},
  {PERCENTILE_DISC, "PERCENTILE_DISC", 1},
  {POSITION, "POSITION", 0},
  {PRECISION, "PRECISION", 0},
  {PREPARE, "PREPARE", 0},
  {PRESERVE, "PRESERVE", 0},
  {PRIMARY, "PRIMARY", 0},
  {PRINT, "PRINT", 1},
  {PRIOR, "PRIOR", 0},
  {PRIORITY, "PRIORITY", 1},
  {PRIVILEGES, "PRIVILEGES", 0},
  {PROCEDURE, "PROCEDURE", 0},
  {QUARTER, "QUARTER", 1},
  {QUERY, "QUERY", 0},
  {QUEUES, "QUEUES", 1},
  {RANGE_, "RANGE", 1},
  {RANK, "RANK", 1},
  {READ, "READ", 0},
  {RECURSIVE, "RECURSIVE", 0},
  {REF, "REF", 0},
  {REFERENCES, "REFERENCES", 0},
  {REFERENCING, "REFERENCING", 0},
  {REJECT_, "REJECT", 1},
  {REMOVE, "REMOVE", 1},
  {RENAME, "RENAME", 0},
  {REORGANIZE, "REORGANIZE", 1},
  {REPEATABLE, "REPEATABLE", 1},
  {REPLACE, "REPLACE", 0},
  {RESIGNAL, "RESIGNAL", 0},
  {RESPECT, "RESPECT", 1},
  {RESTRICT, "RESTRICT", 0},
  {RETAIN, "RETAIN", 1},
  {RETURN, "RETURN", 0},
  {RETURNS, "RETURNS", 0},
  {REVOKE, "REVOKE", 0},
  {REVERSE, "REVERSE", 1},
  {RIGHT, "RIGHT", 0},
  {ROLE, "ROLE", 0},
  {ROLLBACK, "ROLLBACK", 0},
  {ROLLUP, "ROLLUP", 0},
  {ROUTINE, "ROUTINE", 0},
  {ROW, "ROW", 0},
  {ROW_NUMBER, "ROW_NUMBER", 1},
  {ROWNUM, "ROWNUM", 0},
  {ROWS, "ROWS", 0},
  {SAVEPOINT, "SAVEPOINT", 0},
  {SCHEMA, "SCHEMA", 0},
  {SCOPE, "SCOPE___", 0},
  {SCROLL, "SCROLL", 0},
  {SEARCH, "SEARCH", 0},
  {SECOND_, "SECOND", 0},
  {SECOND_MILLISECOND, "SECOND_MILLISECOND", 0},
  {MILLISECOND_, "MILLISECOND", 0},
  {SECTION, "SECTION", 0},
  {SECTIONS, "SECTIONS", 1},
  {SELECT, "SELECT", 0},
  {SENSITIVE, "SENSITIVE", 0},
  {SEPARATOR, "SEPARATOR", 1},
  {SEQUENCE, "SEQUENCE", 0},
  {SEQUENCE_OF, "SEQUENCE_OF", 0},
  {SERIAL, "SERIAL", 1},
  {SERIALIZABLE, "SERIALIZABLE", 0},
  {SESSION, "SESSION", 0},
  {SESSION_USER, "SESSION_USER", 0},
  {SET, "SET", 0},
  {SETEQ, "SETEQ", 0},
  {SETNEQ, "SETNEQ", 0},
  {SET_OF, "SET_OF", 0},
  {SHARED, "SHARED", 0},
  {SHOW, "SHOW", 1},
  {SmallInt, "SHORT", 0},
  {SIBLINGS, "SIBLINGS", 0},
  {SIGNAL, "SIGNAL", 0},
  {SIMILAR, "SIMILAR", 0},
  {SLOTTED, "SLOTTED", 1},
  {SLOTS, "SLOTS", 1},
  {STABILITY, "STABILITY", 1},
  {START_, "START", 1},
  {STATEMENT, "STATEMENT", 1},
  {STATISTICS, "STATISTICS", 0},
  {STATUS, "STATUS", 1},
  {STDDEV, "STDDEV", 1},
  {STDDEV_POP, "STDDEV_POP", 1},
  {STDDEV_SAMP, "STDDEV_SAMP", 1},
  {String, "STRING", 0},
  {STR_TO_DATE, "STR_TO_DATE", 1},
  {SUBCLASS, "SUBCLASS", 0},
  {SUBDATE, "SUBDATE", 1},
  {SUBSET, "SUBSET", 0},
  {SUBSETEQ, "SUBSETEQ", 0},
  {SUBSTRING_, "SUBSTRING", 0},
  {SUM, "SUM", 0},
  {SUPERCLASS, "SUPERCLASS", 0},
  {SUPERSET, "SUPERSET", 0},
  {SUPERSETEQ, "SUPERSETEQ", 0},
  {SYS_CONNECT_BY_PATH, "SYS_CONNECT_BY_PATH", 0},
  {SYSTEM, "SYSTEM", 1},
  {SYSTEM_USER, "SYSTEM_USER", 0},
  {SYS_DATE, "SYS_DATE", 0},
  {SYS_TIME_, "SYS_TIME", 0},
  {SYS_DATETIME, "SYS_DATETIME", 0},
  {SYS_TIMESTAMP, "SYS_TIMESTAMP", 0},
  {SYS_DATE, "SYSDATE", 0},
  {SYS_TIME_, "SYSTIME", 0},
  {SYS_DATETIME, "SYSDATETIME", 0},
  {SYS_TIMESTAMP, "SYSTIMESTAMP", 0},
  {TABLE, "TABLE", 0},
  {TABLES, "TABLES", 1},
  {TEMPORARY, "TEMPORARY", 0},
  {TEXT, "TEXT", 1},
  {THAN, "THAN", 1},
  {THEN, "THEN", 0},
  {THREADS, "THREADS", 1},
  {Time, "TIME", 0},
  {TIMEOUT, "TIMEOUT", 1},
  {TIMESTAMP, "TIMESTAMP", 0},
  {TIMESTAMPTZ, "TIMESTAMPTZ", 0},
  {TIMESTAMPLTZ, "TIMESTAMPLTZ", 0},
  {DATETIME, "DATETIME", 0},
  {DATETIMETZ, "DATETIMETZ", 0},
  {DATETIMELTZ, "DATETIMELTZ", 0},
  {TIMEZONE_HOUR, "TIMEZONE_HOUR", 0},
  {TIMEZONE_MINUTE, "TIMEZONE_MINUTE", 0},
  {TO, "TO", 0},
  {TRACE, "TRACE", 1},
  {TRAILING_, "TRAILING", 0},
  {TRAN, "TRAN", 1},
  {TRANSACTION, "TRANSACTION", 0},
  {TRANSLATE, "TRANSLATE", 0},
  {TRANSLATION, "TRANSLATION", 0},
  {TRIGGER, "TRIGGER", 0},
  {TRIGGERS, "TRIGGERS", 1},
  {TRIM, "TRIM", 0},
  {TRUNCATE, "TRUNCATE", 0},
  {True, "TRUE", 0},
  {UCASE, "UCASE", 1},
  {UNDER, "UNDER", 0},
  {Union, "UNION", 0},
  {UNIQUE, "UNIQUE", 0},
  {UNKNOWN, "UNKNOWN", 0},
  {UPDATE, "UPDATE", 0},
  {UPPER, "UPPER", 0},
  {USAGE, "USAGE", 0},
  {USE, "USE", 0},
  {USER, "USER", 0},
  {USING, "USING", 0},
  {Utime, "UTIME", 0},
  {VACUUM, "VACUUM", 0},
  {VALUE, "VALUE", 0},
  {VALUES, "VALUES", 0},
  {VARCHAR, "VARCHAR", 0},
  {VARIABLE_, "VARIABLE", 0},
  {VARIANCE, "VARIANCE", 1},
  {VAR_POP, "VAR_POP", 1},
  {VAR_SAMP, "VAR_SAMP", 1},
  {VARYING, "VARYING", 0},
  {VCLASS, "VCLASS", 0},
  {VIEW, "VIEW", 0},
  {VOLUME, "VOLUME", 1},
  {WEEK, "WEEK", 1},
  {WHEN, "WHEN", 0},
  {WHENEVER, "WHENEVER", 0},
  {WHERE, "WHERE", 0},
  {WHILE, "WHILE", 0},
  {WITH, "WITH", 0},
  {WITHIN, "WITHIN", 1},
  {WITHOUT, "WITHOUT", 0},
  {WORK, "WORK", 0},
  {WORKSPACE, "WORKSPACE", 1},
  {WRITE, "WRITE", 0},
  {XOR, "XOR", 0},
  {YEAR_, "YEAR", 0},
  {YEAR_MONTH, "YEAR_MONTH", 0},
  {ZONE, "ZONE", 0},
  {TIMEZONES, "TIMEZONES", 1}
};

static KEYWORD_RECORD *pt_find_keyword (const char *text);
static int keyword_cmp (const void *k1, const void *k2);

/* Function names must be written in lowercase */
static FUNCTION_MAP functions[] = {
  {0, "abs", PT_ABS},
  {0, "acos", PT_ACOS},
  {0, "addtime", PT_ADDTIME},
  {0, "asin", PT_ASIN},
  {0, "atan", PT_ATAN},
  {0, "atan2", PT_ATAN2},
  {0, "bin", PT_BIN},
  {0, "bit_count", PT_BIT_COUNT},
  {0, "bit_to_blob", PT_BIT_TO_BLOB},
  {0, "blob_from_file", PT_BLOB_FROM_FILE},
  {0, "blob_length", PT_BLOB_LENGTH},
  {0, "blob_to_bit", PT_BLOB_TO_BIT},
  {0, "ceil", PT_CEIL},
  {0, "ceiling", PT_CEIL},
  {0, "char_length", PT_CHAR_LENGTH},
  {0, "char_to_blob", PT_CHAR_TO_BLOB},
  {0, "char_to_clob", PT_CHAR_TO_CLOB},
  {0, "character_length", PT_CHAR_LENGTH},
  {0, "clob_from_file", PT_CLOB_FROM_FILE},
  {0, "clob_length", PT_CLOB_LENGTH},
  {0, "concat", PT_CONCAT},
  {0, "concat_ws", PT_CONCAT_WS},
  {0, "cos", PT_COS},
  {0, "cot", PT_COT},
  {0, "cume_dist", PT_CUME_DIST},
  {0, "curtime", PT_CURRENT_TIME},
  {0, "curdate", PT_CURRENT_DATE},
  {0, "utc_time", PT_UTC_TIME},
  {0, "utc_date", PT_UTC_DATE},
  {0, "datediff", PT_DATEDIFF},
  {0, "timediff", PT_TIMEDIFF},
  {0, "date_format", PT_DATE_FORMAT},
  {0, "dayofmonth", PT_DAYOFMONTH},
  {0, "dayofyear", PT_DAYOFYEAR},
  {0, "decode", PT_DECODE},
  {0, "decr", PT_DECR},
  {0, "degrees", PT_DEGREES},
  {0, "drand", PT_DRAND},
  {0, "drandom", PT_DRANDOM},
  {0, "exec_stats", PT_EXEC_STATS},
  {0, "exp", PT_EXP},
  {0, "field", PT_FIELD},
  {0, "floor", PT_FLOOR},
  {0, "from_days", PT_FROMDAYS},
  {0, "greatest", PT_GREATEST},
  {0, "groupby_num", PT_GROUPBY_NUM},
  {0, "incr", PT_INCR},
  {0, "index_cardinality", PT_INDEX_CARDINALITY},
  {0, "inst_num", PT_INST_NUM},
  {0, "instr", PT_INSTR},
  {0, "instrb", PT_INSTR},
  {0, "last_day", PT_LAST_DAY},
  {0, "length", PT_CHAR_LENGTH},
  {0, "lengthb", PT_CHAR_LENGTH},
  {0, "least", PT_LEAST},
  {0, "like_match_lower_bound", PT_LIKE_LOWER_BOUND},
  {0, "like_match_upper_bound", PT_LIKE_UPPER_BOUND},
  {0, "list_dbs", PT_LIST_DBS},
  {0, "locate", PT_LOCATE},
  {0, "ln", PT_LN},
  {0, "log2", PT_LOG2},
  {0, "log10", PT_LOG10},
  {0, "log", PT_LOG},
  {0, "lpad", PT_LPAD},
  {0, "ltrim", PT_LTRIM},
  {0, "makedate", PT_MAKEDATE},
  {0, "maketime", PT_MAKETIME},
  {0, "mid", PT_MID},
  {0, "months_between", PT_MONTHS_BETWEEN},
  {0, "new_time", PT_NEW_TIME},
  {0, "format", PT_FORMAT},
  {0, "now", PT_CURRENT_DATETIME},
  {0, "nvl", PT_NVL},
  {0, "nvl2", PT_NVL2},
  {0, "orderby_num", PT_ORDERBY_NUM},
  {0, "percent_rank", PT_PERCENT_RANK},
  {0, "power", PT_POWER},
  {0, "pow", PT_POWER},
  {0, "pi", PT_PI},
  {0, "radians", PT_RADIANS},
  {0, "rand", PT_RAND},
  {0, "random", PT_RANDOM},
  {0, "repeat", PT_REPEAT},
  {0, "space", PT_SPACE},
  {0, "reverse", PT_REVERSE},
  {0, "disk_size", PT_DISK_SIZE},
  {0, "round", PT_ROUND},
  {0, "row_count", PT_ROW_COUNT},
  {0, "last_insert_id", PT_LAST_INSERT_ID},
  {0, "rpad", PT_RPAD},
  {0, "rtrim", PT_RTRIM},
  {0, "sec_to_time", PT_SECTOTIME},
  {0, "serial_current_value", PT_CURRENT_VALUE},
  {0, "serial_next_value", PT_NEXT_VALUE},
  {0, "sign", PT_SIGN},
  {0, "sin", PT_SIN},
  {0, "sqrt", PT_SQRT},
  {0, "strcmp", PT_STRCMP},
  {0, "substr", PT_SUBSTRING},
  {0, "substring_index", PT_SUBSTRING_INDEX},
  {0, "find_in_set", PT_FINDINSET},
  {0, "md5", PT_MD5},
/*
 * temporarily block aes_encrypt and aes_decrypt functions until binary string charset is available.
 *
 *  {0, "aes_encrypt", PT_AES_ENCRYPT},
 *  {0, "aes_decrypt", PT_AES_DECRYPT},
 */
  {0, "sha1", PT_SHA_ONE},
  {0, "sha2", PT_SHA_TWO},
  {0, "substrb", PT_SUBSTRING},
  {0, "tan", PT_TAN},
  {0, "time_format", PT_TIME_FORMAT},
  {0, "to_char", PT_TO_CHAR},
  {0, "to_date", PT_TO_DATE},
  {0, "to_datetime", PT_TO_DATETIME},
  {0, "to_days", PT_TODAYS},
  {0, "time_to_sec", PT_TIMETOSEC},
  {0, "to_number", PT_TO_NUMBER},
  {0, "to_time", PT_TO_TIME},
  {0, "to_timestamp", PT_TO_TIMESTAMP},
  {0, "trunc", PT_TRUNC},
  {0, "tz_offset", PT_TZ_OFFSET},
  {0, "unix_timestamp", PT_UNIX_TIMESTAMP},
  {0, "typeof", PT_TYPEOF},
  {0, "from_unixtime", PT_FROM_UNIXTIME},
  {0, "from_tz", PT_FROM_TZ},
  {0, "weekday", PT_WEEKDAY},
  {0, "dayofweek", PT_DAYOFWEEK},
  {0, "version", PT_VERSION},
  {0, "quarter", PT_QUARTERF},
  {0, "week", PT_WEEKF},
  {0, "hex", PT_HEX},
  {0, "ascii", PT_ASCII},
  {0, "conv", PT_CONV},
  {0, "inet_aton", PT_INET_ATON},
  {0, "inet_ntoa", PT_INET_NTOA},
  {0, "coercibility", PT_COERCIBILITY},
  {0, "width_bucket", PT_WIDTH_BUCKET},
  {0, "trace_stats", PT_TRACE_STATS},
  {0, "str_to_date", PT_STR_TO_DATE},
  {0, "to_base64", PT_TO_BASE64},
  {0, "from_base64", PT_FROM_BASE64},
  {0, "sys_guid", PT_SYS_GUID},
  {0, "sleep", PT_SLEEP},
  {0, "to_datetime_tz", PT_TO_DATETIME_TZ},
  {0, "to_timestamp_tz", PT_TO_TIMESTAMP_TZ},
  {0, "utc_timestamp", PT_UTC_TIMESTAMP},
  {0, "crc32", PT_CRC32},
  {0, "schema_def", PT_SCHEMA_DEF},
  {0, "conv_tz", PT_CONV_TZ},
};


/* The GET_KEYWORD_HASH_VALUE() macro is the definition of the djb2 algorithm as a macro.
 * Refer to the string_hash() function implemented in the libcubmemc.c file.
 */
#define GET_KEYWORD_HASH_VALUE(h,s) \
  do { \
      unsigned char* p = (unsigned char*)(s); \
      for((h) = 5381;  *p; p++ ) \
        { \
             (h) = (((h) << 5) + (h)) + *p; /* hash * 33 + c */ \
        } \
  } while(0)

#define MAGIC_NUM_BI_SEQ        (5)	/* Performance intersection between binary and sequential search */

typedef struct
{
  int min_len;
  int max_len;
  int cnt;
  int (*func_char_convert) (int);
  short start_pos[257];		// (0x00 ~ 0xFF) + 1  
} KEYWORDS_TABLE_SRCH_INFO;

template < typename T > static int
keyword_hash_comparator (const void *lhs, const void *rhs)
{
  int cmp = ((T *) lhs)->hash_value - ((T *) rhs)->hash_value;

  return cmp ? cmp : strcmp (((T *) lhs)->keyword, ((T *) rhs)->keyword);
}

template < typename T, typename Func > void
init_keyword_tables (T & keywords, KEYWORDS_TABLE_SRCH_INFO & info, Func func_cmp)
{
  int i, len;

  for (i = 0; i < info.cnt; i++)
    {
      len = strlen (keywords[i].keyword);
      if (len < info.min_len)
	{
	  info.min_len = len;
	}
      if (len > info.max_len)
	{
	  info.max_len = len;
	}

      GET_KEYWORD_HASH_VALUE (keywords[i].hash_value, keywords[i].keyword);
    }

  qsort (keywords, info.cnt, sizeof (keywords[0]), func_cmp);

  for (i = 0; i < info.cnt; i++)
    {
      info.start_pos[((keywords[i].hash_value) >> 8)]++;
    }

  info.start_pos[256] = info.cnt;
  for (i = 255; i >= 0; i--)
    {
      info.start_pos[i] = info.start_pos[i + 1] - info.start_pos[i];
    }
}


template < typename T, typename T2, typename Func > T2 *
find_keyword_tables (T & keywords, T2 & dummy, KEYWORDS_TABLE_SRCH_INFO & info, Func func_cmp, const char *text)
{
  int len;
  if (!text)
    {
      return NULL;
    }

  len = strlen (text);
  if (len < info.min_len || len > info.max_len)
    {
      return NULL;
    }

  unsigned char *p, *s;
  s = (unsigned char *) dummy.keyword;

  /* Keywords are composed of ASCII characters(English letters, underbar).  */
  /* Function name are composed of ASCII characters.  */
  for (p = (unsigned char *) text; *p; p++, s++)
    {
      if (*p >= 0x80)
	{
	  return NULL;
	}

      *s = (unsigned char) info.func_char_convert ((int) *p);
    }
  *s = 0x00;

  GET_KEYWORD_HASH_VALUE (dummy.hash_value, dummy.keyword);
  int idx, pos, cmp;
  idx = (dummy.hash_value >> 8);
  len = (info.start_pos[idx + 1] - info.start_pos[idx]);
  if (len <= MAGIC_NUM_BI_SEQ)
    {
      for (pos = info.start_pos[idx]; pos < info.start_pos[idx + 1]; pos++)
	{
	  cmp = func_cmp (&dummy, &(keywords[pos]));
	  if (cmp > 0)
	    {
	      continue;
	    }

	  return (cmp ? NULL : (keywords + pos));
	}

      return NULL;
    }

  return (T2 *) bsearch (&dummy, keywords + info.start_pos[idx], len, sizeof (keywords[0]), func_cmp);
}

#ifndef NDEBUG
void
verify_test (bool is_keywords, KEYWORDS_TABLE_SRCH_INFO & info)
{
  int i;

  if (is_keywords)
    {
      KEYWORD_RECORD *pk;
      for (i = 0; i < info.cnt; i++)
	{
	  pk = pt_find_keyword (keywords[i].keyword);
	  assert (pk);
	  assert (strcasecmp (pk->keyword, keywords[i].keyword) == 0);
	}

      for (i = 0; i < sizeof (functions) / sizeof (functions[0]); i++)
	{
	  pk = pt_find_keyword (functions[i].keyword);
	  if (pk)
	    {
	      assert (strcasecmp (pk->keyword, functions[i].keyword) == 0);
	    }
	}
    }
  else
    {
      FUNCTION_MAP *pf;
      for (i = 0; i < info.cnt; i++)
	{
	  pf = pt_find_function_name (functions[i].keyword);
	  assert (pf);
	  assert (strcasecmp (pf->keyword, functions[i].keyword) == 0);
	}

      for (i = 0; i < sizeof (keywords) / sizeof (keywords[0]); i++)
	{
	  pf = pt_find_function_name (keywords[i].keyword);
	  if (pf)
	    {
	      assert (strcasecmp (pf->keyword, keywords[i].keyword) == 0);
	    }
	}
    }
}
#endif
/*
 * pt_find_keyword () -
 *   return: keyword record corresponding to keyword text
 *   text(in): text to test
 */
static KEYWORD_RECORD *
pt_find_keyword (const char *text)
{
  static bool keyword_sorted = false;
  static KEYWORDS_TABLE_SRCH_INFO kinfo;
  int i, len, cmp;
  KEYWORD_RECORD dummy;

  if (keyword_sorted == false)
    {
      kinfo.min_len = MAX_KEYWORD_SIZE;
      kinfo.max_len = 0;
      kinfo.cnt = sizeof (keywords) / sizeof (keywords[0]);
      memset (kinfo.start_pos, 0x00, sizeof (kinfo.start_pos));
      kinfo.func_char_convert = char_toupper;

      init_keyword_tables (keywords, kinfo, keyword_hash_comparator < KEYWORD_RECORD >);

      keyword_sorted = true;

#ifndef NDEBUG
      verify_test (true, kinfo);
#endif
    }

  return (KEYWORD_RECORD *) find_keyword_tables (keywords, dummy, kinfo, keyword_hash_comparator < KEYWORD_RECORD >,
						 text);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * pt_identifier_or_keyword () -
 *   return: token number of corresponding keyword
 *   text(in): text to test
 */
int
pt_identifier_or_keyword (const char *text)
{
  KEYWORD_RECORD *keyword_rec;

  keyword_rec = pt_find_keyword (text);

  if (keyword_rec)
    {
      return keyword_rec->value;
    }
  else
    {
      return IdName;
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * pt_is_reserved_word () -
 *   return: true if string is a keyword
 *   text(in): text to test
 */
bool
pt_is_reserved_word (const char *text)
{
  KEYWORD_RECORD *keyword_rec;

  keyword_rec = pt_find_keyword (text);

  if (!keyword_rec)
    {
      return false;
    }
  else if (keyword_rec->unreserved)
    {
      return false;
    }
  else
    {
      return true;
    }
}


/*
 * pt_is_keyword () -
 *   return:
 *   text(in):
 */
bool
pt_is_keyword (const char *text)
{
  KEYWORD_RECORD *keyword_rec;

  keyword_rec = pt_find_keyword (text);

  if (!keyword_rec)
    {
      return false;
    }
  else
    {
      return true;
    }

/*  else if (keyword_rec->value == NEW || keyword_rec->value == OLD)
    {
      return false;
    }
  else
    {
      return true;
    }
*/
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * pt_get_keyword_rec () -
 *   return: KEYWORD_RECORD
 *   rec_count(out): keywords record count
 */
KEYWORD_RECORD *
pt_get_keyword_rec (int *rec_count)
{
  *(rec_count) = sizeof (keywords) / sizeof (keywords[0]);

  return (KEYWORD_RECORD *) (keywords);
}
#endif

FUNCTION_MAP *
pt_find_function_name (const char *text)
{
  static bool function_keyword_sorted = false;
  static KEYWORDS_TABLE_SRCH_INFO finfo;
  int i, len, cmp;
  FUNCTION_MAP dummy;

  if (function_keyword_sorted == false)
    {
      finfo.min_len = MAX_KEYWORD_SIZE;
      finfo.max_len = 0;
      finfo.cnt = sizeof (functions) / sizeof (functions[0]);
      memset (finfo.start_pos, 0x00, sizeof (finfo.start_pos));
      finfo.func_char_convert = char_tolower;

      init_keyword_tables (functions, finfo, keyword_hash_comparator < FUNCTION_MAP >);

      function_keyword_sorted = true;

#ifndef NDEBUG
      verify_test (false, finfo);
#endif
    }

  char temp[MAX_KEYWORD_SIZE];

  dummy.keyword = temp;
  return (FUNCTION_MAP *) find_keyword_tables (functions, dummy, finfo, keyword_hash_comparator < FUNCTION_MAP >, text);
}
