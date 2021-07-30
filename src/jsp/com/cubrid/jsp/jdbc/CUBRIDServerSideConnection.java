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

import cubrid.jdbc.jci.UJCIUtil;
import java.util.logging.Logger;

import com.cubrid.jsp.ExecuteThread;

import com.cubrid.jsp.data.CUBRIDPacker;
import java.nio.ByteBuffer;
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

import java.net.Socket;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDServerSideConnection implements Connection {
    private final static Logger LOG = Logger.getGlobal();

    // Transaction Isolation Level Constants ported from CUBRIDConnection
    public static final int TRAN_REP_CLASS_REP_INSTANCE = TRANSACTION_REPEATABLE_READ; // 4
    public static final int TRAN_REP_CLASS_COMMIT_INSTANCE = 16;
    public static final int TRAN_REP_CLASS_UNCOMMIT_INSTANCE = 32;
    public static final int TRAN_SERIALIZABLE = TRANSACTION_SERIALIZABLE;

    // Request Function Code
    public static final int REQ_FUNCTION_PREPARE = 2; // UFunctionCode.PREPARE;
    public static final int REQ_FUNCTION_EXECUTE = 3; // UFunctionCode.EXECUTE;
    public static final int REQ_FUNCTION_CURSOR = 7; // UFunctionCode.CURSOR;
    public static final int REQ_FUNCTION_FETCH = 8; // UFunctionCode.FETCH;
    public static final int REQ_FUNCTION_GET_SCHEMA_INFO = 9; // UFunctionCode.GET_SCHEMA_INFO;
    public static final int REQ_FUNCTION_GET_NEXT_RESULT = 19; // UFunctionCode.NEXT_RESULT;
    public static final int REQ_FUNCTION_MAKE_OUT_RS = 33; // UFunctionCode.MAKE_OUT_RS;
    public static final int REQ_FUNCTION_GET_GENERATED_KEYS = 34; // UFunctionCode.GET_GENERATED_KEYS;
    public static final int REQ_FUNCTION_CURSOR_CLOSE = 34; // UFunctionCode.CURSOR_CLOSE;

    int transactionIsolation;
    int holdability;
    protected CUBRIDServerSideDatabaseMetaData mdata = null;
    protected List<Statement> statements = null;

    public CUBRIDServerSideConnection() {
        holdability = ResultSet.HOLD_CURSORS_OVER_COMMIT; // there is no meaning for the holdable cursor on server-side
        transactionIsolation = TRANSACTION_NONE;

        statements = new ArrayList<Statement> ();
        LOG.info ("CUBRIDServerSideConnection constructor");
    }

    ExecuteThread thread = null;
    public void setThread (ExecuteThread t) {
        thread = t;
    }

    public boolean requestPrepare (String sql, int flag) {
        try {
            ByteBuffer buffer = ByteBuffer.allocate(sql.length() + Integer.BYTES);
            CUBRIDPacker packer = new CUBRIDPacker (buffer);
            
            packer.packInt (REQ_FUNCTION_PREPARE);
            packer.packString (sql);
            packer.packInt (flag);

            // thread.sendCommand(packer.getBuffer());
            } catch (Exception e) {
                return false;
            }
        return true;
    }

    public boolean requestExecute () {
        return true;
    }

    /* To manage List<Statement> statements */
    public void addStatement (Statement s) {
        this.statements.add(s);
    }

    public void removeStatement(Statement s) throws SQLException {
        int i = statements.indexOf(s);
        if (i > -1) {
            statements.remove(i);
        }
    }

    // ==============================================================
    // The following is JDBC Interface Implementations
    // ==============================================================

    public Statement createStatement() throws SQLException {
        return createStatement(ResultSet.TYPE_FORWARD_ONLY, ResultSet.CONCUR_READ_ONLY);
    }

    public PreparedStatement prepareStatement(String sql) throws SQLException {
        LOG.info ("prepareStatement");
        return prepareStatement(
            sql, 
            ResultSet.TYPE_FORWARD_ONLY,
            ResultSet.CONCUR_READ_ONLY,
            holdability);
    }

    public CallableStatement prepareCall(String sql) throws SQLException {
        CallableStatement cstmt = new CUBRIDServerSideCallableStatement(this, sql);
        addStatement (cstmt);
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
        /* The connection is an implicit data channel, not an explicit connection instance as from a client. */
        /* do nothing */
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
        // TODO
        // It is able to get at DB Server
        // int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
        // TRAN_ISOLATION tran_isolation = logtb_find_isolation (tran_index);
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
        Statement stmt = new CUBRIDServerSideStatement(this, resultSetType, resultSetConcurrency, holdability);
        addStatement (stmt);
        return stmt;
    }

    public PreparedStatement prepareStatement(
            String sql, int resultSetType, int resultSetConcurrency) throws SQLException {
        return prepareStatement (sql, resultSetType, resultSetConcurrency, holdability);
    }

    public CallableStatement prepareCall(String sql, int resultSetType, int resultSetConcurrency)
            throws SQLException {
        return prepareCall (sql);
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

    public Statement createStatement(int resultSetType, int resultSetConcurrency, int holdable) throws SQLException {
        if (holdable == ResultSet.HOLD_CURSORS_OVER_COMMIT) {
            if (resultSetType == ResultSet.TYPE_SCROLL_SENSITIVE || resultSetConcurrency == ResultSet.CONCUR_UPDATABLE) {
                throw new SQLException(new java.lang.UnsupportedOperationException());
            }
        }
        Statement stmt = new CUBRIDServerSideStatement(this, resultSetType, resultSetConcurrency, holdable);
        addStatement (stmt);
        return stmt;
    }

    public PreparedStatement prepareStatement(
            String sql, int type, int concur, int holdable) throws SQLException {
        if (holdable == ResultSet.HOLD_CURSORS_OVER_COMMIT) {
            if (type == ResultSet.TYPE_SCROLL_SENSITIVE || concur == ResultSet.CONCUR_UPDATABLE) {
                throw new SQLException(new java.lang.UnsupportedOperationException());
            }
        }
        PreparedStatement stmt = new CUBRIDServerSidePreparedStatement 
            (this, sql, type, concur, holdable, Statement.NO_GENERATED_KEYS);
        statements.add (stmt);
        return stmt;
    }

    public CallableStatement prepareCall(String sql, int type, int concur, int holdable)
            throws SQLException {
        return prepareCall(sql);
    }

    public PreparedStatement prepareStatement(String sql, int autoGeneratedKeys)
            throws SQLException {
        PreparedStatement stmt = new CUBRIDServerSidePreparedStatement 
            (this, 
            sql, 
            ResultSet.TYPE_FORWARD_ONLY, 
            ResultSet.CONCUR_READ_ONLY, 
            holdability, 
            autoGeneratedKeys);
        statements.add (stmt);
        return stmt;
    }

    public PreparedStatement prepareStatement(String sql, int[] indexes)
            throws SQLException {
        return prepareStatement(sql);
    }

    public PreparedStatement prepareStatement(String sql, String[] colName)
            throws SQLException {
        return prepareStatement(sql);
    }

    /* JDK 1.6 */
    public Clob createClob() throws SQLException {
        // TODO
        // Clob clob = new CUBRIDClob(this, getUConnection().getCharset());
        return null;
    }

    /* JDK 1.6 */
    public Blob createBlob() throws SQLException {
        // TODO
        // Blob blob = new CUBRIDBlob(this);
        return null;
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

        // it have to be always valid
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
        throw new SQLException(new java.lang.UnsupportedOperationException());
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
