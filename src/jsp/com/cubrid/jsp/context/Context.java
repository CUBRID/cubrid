package com.cubrid.jsp.context;

import com.cubrid.jsp.ExecuteThread;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConnection;
import com.cubrid.jsp.protocol.Header;
import java.nio.ByteBuffer;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.Properties;
import java.util.concurrent.LinkedBlockingQueue;

public class Context {
    // To recognize unique DB session
    private long sessionId = -1;

    // request Id (for future)
    private int prevRequestId = 0;

    // charset
    private String charSet = "UTF-8";

    // single server-side connection per Context
    private CUBRIDServerSideConnection connection = null;

    private LinkedBlockingQueue<ByteBuffer> inBound = null;

    // CAS client information connecting with this Context
    private Properties clientInfo = null;

    // TODO: dynamic classLoader for a session

    public Context(long id) {
        sessionId = id;
    }

    public long getSessionId() {
        return sessionId;
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

    public void checkHeader(Header header) {
        if (prevRequestId > header.requestId) {
            // not incremented
            // a new session is started with the same session Id or the trasaction is ended
            clear();
        }
        prevRequestId = header.requestId;
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

    // TODO: move this function to proper place
    public static ExecuteThread getCurrentExecuteThread() {
        return (ExecuteThread) Thread.currentThread();
    }
}
