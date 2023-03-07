package com.cubrid.jsp.context;

import com.cubrid.jsp.ExecuteThread;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConnection;
import java.nio.ByteBuffer;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.Properties;
import java.util.concurrent.LinkedBlockingQueue;

public class Context {
    // To recognize unique DB session
    private long sessionId = -1;

    // If transaction ID is changed, The server-side connection must be reset
    private long transactionId = -1;

    // charset
    private String charSet = "UTF-8";

    // single server-side connection per Context
    private CUBRIDServerSideConnection connection = null;

    private LinkedBlockingQueue<ByteBuffer> inBound = null;

    // CAS client information connecting with this Context
    private Properties clientInfo = null;

    // TODO: dynamic classLoader

    public Context(long id) {
        sessionId = id;
    }

    public long getSessionId() {
        return sessionId;
    }

    public long getTransactionId() {
        return transactionId;
    }

    public void setTransactionId(long transactionId) {
        this.transactionId = transactionId;
    }

    public synchronized Connection getConnection() {
        if (this.connection == null) {
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

    // TODO: move this function to proper place
    public static ExecuteThread getCurrentExecuteThread() {
        return (ExecuteThread) Thread.currentThread();
    }
}
