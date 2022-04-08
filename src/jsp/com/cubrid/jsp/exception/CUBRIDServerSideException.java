package com.cubrid.jsp.exception;

import com.cubrid.jsp.jdbc.CUBRIDServerSideJDBCErrorCode;
import java.sql.SQLException;

@SuppressWarnings("serial")
public class CUBRIDServerSideException extends SQLException {

    protected CUBRIDServerSideException(String msg, int errCode) {
        super(msg, null, errCode);
    }

    public CUBRIDServerSideException(int errCode, Throwable t) {
        this(CUBRIDServerSideJDBCErrorCode.codeToMessage(errCode, null), errCode);
        if (t != null) {
            setStackTrace(t.getStackTrace());
        }
    }

    public CUBRIDServerSideException(int errCode) {
        this(CUBRIDServerSideJDBCErrorCode.codeToMessage(errCode, null), errCode);
    }

    public CUBRIDServerSideException(int errCode, String msg, Throwable t) {
        this(CUBRIDServerSideJDBCErrorCode.codeToMessage(errCode, msg), errCode);
        if (t != null) {
            setStackTrace(t.getStackTrace());
        }
    }
}
