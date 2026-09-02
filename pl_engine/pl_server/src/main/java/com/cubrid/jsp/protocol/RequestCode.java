package com.cubrid.jsp.protocol;

public class RequestCode {
    public static final int INVOKE_SP = 0x01;
    public static final int RESULT = 0x02;
    public static final int ERROR = 0x04;
    public static final int INTERNAL_JDBC = 0x08;

    public static final int DESTROY = 0x10;

    public static final int COMPILE = 0x80;

    public static final int REQUEST_SQL_SEMANTICS = 0xA0;
    public static final int REQUEST_GLOBAL_SEMANTICS = 0xA1;

    public static final int REQUEST_CODE_ATTR = 0xC9;

    public static final int UTIL_BOOTSTRAP = 0xDD;
    public static final int UTIL_PING = 0xDE;
    public static final int UTIL_STATUS = 0xEE;
}
