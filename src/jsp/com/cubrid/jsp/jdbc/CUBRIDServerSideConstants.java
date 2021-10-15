package com.cubrid.jsp.jdbc;

public class CUBRIDServerSideConstants {
    /* statement type */
    public static final byte NORMAL = 0,
            GET_BY_OID = 1,
            GET_SCHEMA_INFO = 2,
            GET_AUTOINCREMENT_KEYS = 3;

    /* prepare flags */
    public static final byte PREPARE_INCLUDE_OID = 0x01,
            PREPARE_UPDATABLE = 0x02,
            PREPARE_QUERY_INFO = 0x04,
            PREPARE_HOLDABLE = 0x08,
            PREPARE_XASL_CACHE_PINNED = 0x10,
            PREPARE_CALL = 0x40;

    /* execute flags */
    public static final byte EXEC_FLAG_ASYNC = 0x01,
            EXEC_FLAG_QUERY_ALL = 0x02,
            EXEC_FLAG_QUERY_INFO = 0x04,
            EXEC_FLAG_ONLY_QUERY_PLAN = 0x08,
            EXEC_FLAG_HOLDABLE_RESULT = 0x20,
            EXEC_FLAG_GET_GENERATED_KEYS = 0x40;
    public static final int CURSOR_SET = 0, CURSOR_CUR = 1, CURSOR_END = 2;
}
