package com.cubrid.jsp.io;

import java.sql.Connection;

public class DBParam {
    public int isolation = Connection.TRANSACTION_NONE;
    public int wait_msec = 0;
}