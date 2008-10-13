package com.cubrid.jsp.value;

import java.sql.ResultSet;

import com.cubrid.jsp.exception.TypeMismatchException;

public class ResultSetValue extends Value {

    private ResultSet rset;
    public ResultSetValue(ResultSet rset) {
        super();
        this.rset = rset;
    }
    
    public ResultSet toResultSet() throws TypeMismatchException {
        return rset;
    }
}
