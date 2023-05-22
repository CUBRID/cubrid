package com.cubrid.jsp.jdbc;

import com.cubrid.jsp.impl.SUConnection;
import java.io.IOException;
import java.sql.SQLException;

public class CUBRIDServerSideOutResultSet extends CUBRIDServerSideResultSet {

    public CUBRIDServerSideOutResultSet(SUConnection ucon, long queryId)
            throws IOException, SQLException {
        super(ucon, queryId);
    }

    public long getQueryId() {
        return getStatementHandler().getQueryId();
    }
}
