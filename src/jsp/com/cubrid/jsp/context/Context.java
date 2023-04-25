/*
 *
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

package com.cubrid.jsp.context;

import com.cubrid.jsp.ExecuteThread;
import com.cubrid.jsp.TargetMethodCache;
import com.cubrid.jsp.classloader.ClassLoaderManager;
import com.cubrid.jsp.classloader.ContextClassLoader;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConnection;
import com.cubrid.jsp.protocol.Header;
import com.cubrid.plcsql.builtin.MessageBuffer;
import java.nio.ByteBuffer;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.Properties;
import java.util.concurrent.LinkedBlockingQueue;

public class Context {
    // To recognize unique DB session
    private long sessionId = -1;

    // transaction Id
    private int tranactionId = -1;

    // request Id (for future)
    private int prevRequestId = 0;

    // charset
    private String charSet = "UTF-8";

    // single server-side connection per Context
    private CUBRIDServerSideConnection connection = null;

    private LinkedBlockingQueue<ByteBuffer> inBound = null;

    // CAS client information connecting with this Context
    private Properties clientInfo = null;

    // dynamic classLoader for a session
    private ContextClassLoader classLoader = null;

    // method cache
    private TargetMethodCache methodCache = null;

    // Whether SP is able to process TCL (commit, rollback). (default: false)
    private boolean transactionControl = false;

    // Connection Properties
    private Properties connectionInfo = null;

    // message buffer for DBMS_OUTPUT
    private MessageBuffer messageBuffer;

    public Context(long id) {
        sessionId = id;
    }

    public long getSessionId() {
        return sessionId;
    }

    public synchronized Connection getConnection(Properties prop) {
        if (this.connection == null) {
            this.connectionInfo = prop;
            this.connection = new CUBRIDServerSideConnection(this);
        }
        return connection;
    }

    public void closeConnection(Connection conn) throws SQLException {
        if (connection != null) {
            connection.close();
        }
    }

    public Properties getClientInfo() {
        if (clientInfo == null) {
            clientInfo = new Properties();
        }
        return clientInfo;
    }

    public LinkedBlockingQueue<ByteBuffer> getInboundQueue() {
        if (inBound == null) {
            inBound = new LinkedBlockingQueue<ByteBuffer>();
        }
        return inBound;
    }

    public String getCharset() {
        return charSet;
    }

    public void checkHeader(Header header) {
        if (prevRequestId > header.requestId) {
            // not incremented
            // a new session is started with the same session Id or the trasaction is ended
            clear();
        }
        prevRequestId = header.requestId;
    }

    public void checkTranId(int tid) {
        if (tranactionId == -1) {
            tranactionId = tid;
        }

        if (tranactionId != tid) {
            // re-cretae dynamic class loader
            if (classLoader
                            .getInitializedTime()
                            .compareTo(
                                    ClassLoaderManager.getLastModifiedTimeOfPath(
                                            ClassLoaderManager.getDynamicPath()))
                    != 0) {
                classLoader = new ContextClassLoader();
                methodCache.clear();
            }
            clear();
            tranactionId = tid;
        }
    }

    public void clear() {
        try {
            closeConnection(connection);
        } catch (Exception e) {
            // ignore
        } finally {
            connection = null;
        }
    }

    public MessageBuffer getMessageBuffer() {
        if (messageBuffer == null) {
            messageBuffer = new MessageBuffer();
        }
        return messageBuffer;
    }

    public ClassLoader getClassLoader() {
        if (classLoader == null) {
            classLoader = new ContextClassLoader();
        }

        return classLoader;
    }

    public TargetMethodCache getTargetMethodCache() {
        if (methodCache == null) {
            methodCache = new TargetMethodCache();
        }

        return methodCache;
    }

    public void setTransactionControl(boolean tc) {
        this.transactionControl = tc;
    }

    public boolean canTransactionControl() {
        if (transactionControl) {
            return true;
        }

        String tcProp = connectionInfo.getProperty("transaction_control");
        if (tcProp != null && "true".equalsIgnoreCase(tcProp)) {
            return true;
        }

        return false;
    }

    // TODO: move this function to proper place
    public static ExecuteThread getCurrentExecuteThread() {
        return (ExecuteThread) Thread.currentThread();
    }
}
