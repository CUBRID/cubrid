/*
 * Copyright (C) 2008 Search Solution Corporation.
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

package com.cubrid.jsp.jdbc;

import com.cubrid.jsp.ExecuteThread;
import com.cubrid.jsp.data.DBParameterInfo;
import com.cubrid.jsp.impl.SUConnection;
import cubrid.jdbc.jci.CUBRIDIsolationLevel;
import java.io.IOException;
import java.sql.Array;
import java.sql.Blob;
import java.sql.CallableStatement;
import java.sql.Clob;
import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.NClob;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLClientInfoException;
import java.sql.SQLException;
import java.sql.SQLWarning;
import java.sql.SQLXML;
import java.sql.Savepoint;
import java.sql.Statement;
import java.sql.Struct;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.Executor;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDServerSideConnection implements Connection {
    private ExecuteThread thread = null;

    protected CUBRIDServerSideDatabaseMetaData mdata = null;
    protected List<Statement> statements = null;
    private SUConnection suConn = null;

    private int transactionIsolation;
    private int holdability;
    private Properties clientInfo = null;

    public CUBRIDServerSideConnection(ExecuteThread thread) {
        this.thread = thread;

        holdability =
                ResultSet.HOLD_CURSORS_OVER_COMMIT; // default value, there is no meaning for the
        // holdable cursor on server-side
        transactionIsolation = TRANSACTION_NONE;

        statements = new ArrayList<Statement>();
    }

    public SUConnection getSUConnection() {
        if (suConn == null) {
            suConn = new SUConnection(thread);
        }
        return suConn;
    }

    protected void requestDBParameter() throws IOException, SQLException {
        DBParameterInfo info = getSUConnection().getDBParameter();
        
        switch (info.tran_isolation) {
            case CUBRIDIsolationLevel.TRAN_READ_COMMITTED:
                transactionIsolation = TRANSACTION_READ_COMMITTED;
                break;

            case CUBRIDIsolationLevel.TRAN_REPEATABLE_READ:
                transactionIsolation = TRANSACTION_REPEATABLE_READ;
                break;

            case CUBRIDIsolationLevel.TRAN_SERIALIZABLE:
                transactionIsolation = TRANSACTION_SERIALIZABLE;
                break;

            default:
                transactionIsolation = TRANSACTION_NONE;
                break;
        }

        // TODO: lock timeout?

        clientInfo = new Properties();
        clientInfo.put("type", String.valueOf(info.clientIds.clientType));
        clientInfo.put("program", info.clientIds.programName);
        clientInfo.put("host", info.clientIds.hostName);
        clientInfo.put("login", info.clientIds.loginName);
        clientInfo.put("user", info.clientIds.dbUser);
        clientInfo.put("ip", info.clientIds.clientIp);
        clientInfo.put("pid", String.valueOf(info.clientIds.processId));
    }

    /* To manage List<Statement> statements */
    public void addStatement(Statement s) {
        this.statements.add(s);
    }

    public void removeStatement(Statement s) throws SQLException {
        int i = statements.indexOf(s);
        if (i > -1) {
            statements.remove(i);
        }
    }

    // ==============================================================
    // The following are JDBC Interface Implementations
    // ==============================================================

    public Statement createStatement() throws SQLException {
        return createStatement(ResultSet.TYPE_FORWARD_ONLY, ResultSet.CONCUR_READ_ONLY);
    }

    public PreparedStatement prepareStatement(String sql) throws SQLException {
        return prepareStatement(
                sql, ResultSet.TYPE_FORWARD_ONLY, ResultSet.CONCUR_READ_ONLY, holdability);
    }

    public CallableStatement prepareCall(String sql) throws SQLException {
        CallableStatement cstmt = new CUBRIDServerSideCallableStatement(this, sql);
        addStatement(cstmt);
        return cstmt;
    }

    public String nativeSQL(String sql) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setAutoCommit(boolean autoCmmit) {
        /* do nothing */
    }

    public boolean getAutoCommit() throws SQLException {
        /* always false */
        return false;
    }

    public void commit() throws SQLException {
        /* do nothing */
    }

    public void rollback() throws SQLException {
        /* do nothing */
    }

    public void close() throws SQLException {
        /* Becuase It is assume that Java SP Server always connecting with DB Server directly, It should not be closed */
        /* Here, only the JDBC resources are cleaned up */
        /* The connection is not actually terminated or database resources such as query handlers and result sets are removed. */
        if (statements != null) {
            for (Statement s : statements) {
                s.close();
            }
            statements.clear();
        }
    }

    public boolean isClosed() throws SQLException {
        /* always false */
        return false;
    }

    public DatabaseMetaData getMetaData() throws SQLException {
        if (mdata == null) {
            mdata = new CUBRIDServerSideDatabaseMetaData(this);
        }
        return mdata;
    }

    public void setReadOnly(boolean arg0) throws SQLException {
        /* do nothing */
    }

    public boolean isReadOnly() throws SQLException {
        /* do nothing */
        return false;
    }

    public void setCatalog(String catalog) throws SQLException {
        /* do nothing */
    }

    public String getCatalog() throws SQLException {
        /* do nothing */
        return "";
    }

    public void setTransactionIsolation(int level) throws SQLException {
        /* do nothing */
        /* transaction isolation should not be set by server-side connection */
    }

    public int getTransactionIsolation() throws SQLException {
        if (transactionIsolation == TRANSACTION_NONE) {
            try {
                requestDBParameter();
            } catch (IOException e) {
                throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                        CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
            }
        }

        return transactionIsolation;
    }

    public SQLWarning getWarnings() throws SQLException {
        /* do nothing */
        return null;
    }

    public void clearWarnings() throws SQLException {
        /* do nothing */
    }

    public Statement createStatement(int resultSetType, int resultSetConcurrency)
            throws SQLException {
        Statement stmt =
                new CUBRIDServerSideStatement(
                        this, resultSetType, resultSetConcurrency, holdability);
        addStatement(stmt);
        return stmt;
    }

    public PreparedStatement prepareStatement(
            String sql, int resultSetType, int resultSetConcurrency) throws SQLException {
        return prepareStatement(sql, resultSetType, resultSetConcurrency, holdability);
    }

    public CallableStatement prepareCall(String sql, int resultSetType, int resultSetConcurrency)
            throws SQLException {
        return prepareCall(sql);
    }

    public Map<String, Class<?>> getTypeMap() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public void setTypeMap(Map<String, Class<?>> map) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public void setHoldability(int holdable) throws SQLException {
        /* do nothing */
    }

    public int getHoldability() throws SQLException {
        /* do nothing, return default value */
        return holdability;
    }

    public Savepoint setSavepoint() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public Savepoint setSavepoint(String name) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public void rollback(Savepoint savepoint) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public void releaseSavepoint(Savepoint savepoint) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public Statement createStatement(int resultSetType, int resultSetConcurrency, int holdable)
            throws SQLException {
        if (holdable == ResultSet.HOLD_CURSORS_OVER_COMMIT) {
            if (resultSetType == ResultSet.TYPE_SCROLL_SENSITIVE
                    || resultSetConcurrency == ResultSet.CONCUR_UPDATABLE) {
                throw new SQLException(new java.lang.UnsupportedOperationException());
            }
        }
        Statement stmt =
                new CUBRIDServerSideStatement(this, resultSetType, resultSetConcurrency, holdable);
        addStatement(stmt);
        return stmt;
    }

    public PreparedStatement prepareStatement(String sql, int type, int concur, int holdable)
            throws SQLException {
        if (holdable == ResultSet.HOLD_CURSORS_OVER_COMMIT) {
            if (type == ResultSet.TYPE_SCROLL_SENSITIVE || concur == ResultSet.CONCUR_UPDATABLE) {
                throw new SQLException(new java.lang.UnsupportedOperationException());
            }
        }

        PreparedStatement stmt =
                new CUBRIDServerSidePreparedStatement(
                        this, sql, type, concur, holdable, Statement.NO_GENERATED_KEYS);

        statements.add(stmt);
        return stmt;
    }

    public CallableStatement prepareCall(String sql, int type, int concur, int holdable)
            throws SQLException {
        return prepareCall(sql);
    }

    public PreparedStatement prepareStatement(String sql, int autoGeneratedKeys)
            throws SQLException {
        PreparedStatement stmt =
                new CUBRIDServerSidePreparedStatement(
                        this,
                        sql,
                        ResultSet.TYPE_FORWARD_ONLY,
                        ResultSet.CONCUR_READ_ONLY,
                        holdability,
                        autoGeneratedKeys);
        statements.add(stmt);
        return stmt;
    }

    public PreparedStatement prepareStatement(String sql, int[] indexes) throws SQLException {
        return prepareStatement(sql);
    }

    public PreparedStatement prepareStatement(String sql, String[] colName) throws SQLException {
        return prepareStatement(sql);
    }

    /* JDK 1.6 */
    public Clob createClob() throws SQLException {
        // TODO: not implemented yet
        // Clob clob = new CUBRIDClob(this, getUConnection().getCharset());
        throw new SQLException(new UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public Blob createBlob() throws SQLException {
        // TODO: not implemented yet
        // Blob blob = new CUBRIDBlob(this);
        throw new SQLException(new UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public NClob createNClob() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public SQLXML createSQLXML() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public boolean isValid(int timeout) throws SQLException {
        if (timeout < 0) {
            throw new SQLException();
        }

        // it has to be always valid
        return true;
    }

    /* JDK 1.6 */
    public void setClientInfo(Properties arg0) throws SQLClientInfoException {
        SQLClientInfoException clientEx = new SQLClientInfoException();
        clientEx.initCause(new java.lang.UnsupportedOperationException());
        throw clientEx;
    }

    /* JDK 1.6 */
    public void setClientInfo(String arg0, String arg1) throws SQLClientInfoException {
        SQLClientInfoException clientEx = new SQLClientInfoException();
        clientEx.initCause(new java.lang.UnsupportedOperationException());
        throw clientEx;
    }

    /* JDK 1.6 */
    public Properties getClientInfo() throws SQLException {
        if (clientInfo == null) {
            try {
                requestDBParameter();
            } catch (IOException e) {
                throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                        CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
            }
        }

        return clientInfo;
    }

    /* JDK 1.6 */
    public String getClientInfo(String arg0) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public Array createArrayOf(String arg0, Object[] arg1) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public Struct createStruct(String arg0, Object[] arg1) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public void setSchema(String schema) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public String getSchema() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public void abort(Executor executor) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public void setNetworkTimeout(Executor executor, int milliseconds) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public int getNetworkTimeout() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    // From java.sql.Wrapper
    /* JDK 1.6 */
    public boolean isWrapperFor(Class<?> arg0) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public <T> T unwrap(Class<T> arg0) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }
}
