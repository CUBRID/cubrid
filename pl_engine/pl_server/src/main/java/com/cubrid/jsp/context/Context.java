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
import com.cubrid.jsp.Server;
import com.cubrid.jsp.ServerConfig;
import com.cubrid.jsp.SysParam;
import com.cubrid.jsp.classloader.CatalogClassLoaderRelay;
import com.cubrid.jsp.classloader.ClassPathHelper;
import com.cubrid.jsp.classloader.FileClassLoaderDynamic;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConnection;
import com.cubrid.plcsql.builtin.MessageBuffer;
import java.nio.ByteBuffer;
import java.nio.charset.Charset;
import java.nio.file.attribute.FileTime;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.Properties;
import java.util.concurrent.LinkedBlockingQueue;

public class Context {
    // To recognize unique DB session
    private long sessionId = -1;

    // transaction Id
    private int tranactionId = -1;

    // charset
    private Charset sessionCharset = null;

    // single server-side connection per Context
    private CUBRIDServerSideConnection connection = null;

    private LinkedBlockingQueue<ByteBuffer> inBound = null;

    // CAS client information connecting with this Context
    private Properties clientInfo = null;

    // dynamic classLoader for a session
    private CatalogClassLoaderRelay catalogClassLoaderRelay = null;
    private FileClassLoaderDynamic fileClassLoader = null; // file

    // Whether SP is able to process TCL (commit, rollback). (default: false)
    private boolean transactionControl = false;

    // Connection Properties
    private static Properties DEFAULT_CONNECTION_INFO = new Properties();
    private Properties connectionInfo = null;

    // message buffer for DBMS_OUTPUT
    private MessageBuffer messageBuffer;

    // context system parameters
    private HashMap<Integer, SysParam> systemParameters = null;

    public Context(long id) {
        sessionId = id;
    }

    public long getSessionId() {
        return sessionId;
    }

    public synchronized Connection getConnection() {
        return getConnection(DEFAULT_CONNECTION_INFO);
    }

    public synchronized Connection getConnection(Properties prop) {
        if (this.connection == null) {
            this.connectionInfo = prop;
            this.connection = new CUBRIDServerSideConnection(this);
        }
        return connection;
    }

    public void closeConnection() throws SQLException {
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

    public HashMap<Integer, SysParam> getSystemParameters() {
        if (systemParameters == null) {
            systemParameters = new HashMap<Integer, SysParam>();
        }
        return systemParameters;
    }

    public void checkTranId(int tid) {
        if (tranactionId == -1) {
            tranactionId = tid;
            fileClassLoader = new FileClassLoaderDynamic();
        } else if (tranactionId != tid) {
            assert fileClassLoader != null;
            FileTime lastModifiedTimeOfDynamicPath =
                    ClassPathHelper.getLastModifiedTimeOfDynamicPath();
            if (fileClassLoader.lastModifiedTimeOfDynamicPath.compareTo(
                            lastModifiedTimeOfDynamicPath)
                    != 0) {
                // re-create dynamic class loader
                fileClassLoader = new FileClassLoaderDynamic(lastModifiedTimeOfDynamicPath);
            }

            // to reload updated classes in a new transaction
            getCatalogClassLoaderRelay().markChildrenAsOld();

            if (connection != null) {
                connection.invalidateStatements();
            }

            tranactionId = tid;
        }
    }

    private void clear() {
        try {
            closeConnection();
        } catch (Exception e) {
            // ignore
        } finally {
            connection = null;
        }
    }

    public void destroy() {
        clear();
        if (catalogClassLoaderRelay != null) {
            catalogClassLoaderRelay.clear();
            catalogClassLoaderRelay = null;
        }

        fileClassLoader = null;

        if (messageBuffer != null) {
            messageBuffer.clear();
        }
    }

    public MessageBuffer getMessageBuffer() {
        if (messageBuffer == null) {
            messageBuffer = new MessageBuffer();
        }
        return messageBuffer;
    }

    public CatalogClassLoaderRelay getCatalogClassLoaderRelay() {
        if (catalogClassLoaderRelay == null) {
            catalogClassLoaderRelay = new CatalogClassLoaderRelay(sessionId);
        }

        return catalogClassLoaderRelay;
    }

    public ClassLoader getFileClassLoader() {
        if (fileClassLoader == null) {
            fileClassLoader = new FileClassLoaderDynamic();
        }

        return fileClassLoader;
    }

    public void setTransactionControl(boolean tc) {
        this.transactionControl = tc;
    }

    public boolean canTransactionControl() {
        if (transactionControl) {
            return true;
        }

        if (connectionInfo != null) {
            String tcProp = connectionInfo.getProperty("transaction_control");
            if (tcProp != null && "true".equalsIgnoreCase(tcProp)) {
                return true;
            }
        }

        return false;
    }

    public static int getCodesetId() {
        return SysParam.getCodesetId(getSessionCharset());
    }

    public static Charset getSessionCharset() {
        Context ctx = ContextManager.getContextofCurrentThread();
        SysParam sysParam = ctx.getSystemParameters().get(SysParam.INTL_COLLATION);
        if (sysParam == null) {
            return Server.getConfig().getServerCharset();
        }

        String collation = sysParam.getParamValue();
        String codeset = ServerConfig.parseCollationString(collation);

        Charset charset;
        try {
            charset = Charset.forName(codeset);
        } catch (Exception e) {
            // java.nio.charset.IllegalCharsetNameException
            // invalid charset is specified
            charset = Server.getConfig().getServerCharset();
        }

        return charset;
    }

    public static SysParam getSystemParam(int id) {
        Context ctx = ContextManager.getContextofCurrentThread();
        SysParam param = ctx.getSystemParameters().get(id);
        if (param == null) {
            // get server's parameter
            param = Server.getConfig().getSystemParameters().get(id);
        }
        return param;
    }

    public static String getSystemParameterString(int id) {
        SysParam param = getSystemParam(id);
        if (param != null) {
            return param.getParamValueString();
        }

        return null;
    }

    public static Boolean getSystemParameterBool(int id) {
        SysParam param = getSystemParam(id);
        if (param != null) {
            return param.getParamValueBoolean();
        }

        return null;
    }

    // TODO: move this function to proper place
    public static ExecuteThread getCurrentExecuteThread() {
        return (ExecuteThread) Thread.currentThread();
    }
}
