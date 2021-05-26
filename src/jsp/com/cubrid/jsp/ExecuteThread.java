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

package com.cubrid.jsp;

import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.data.DataUtilities;
import com.cubrid.jsp.exception.ExecuteException;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.value.Value;
import com.cubrid.jsp.value.ValueUtilities;
import cubrid.jdbc.driver.CUBRIDConnectionDefault;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

public class ExecuteThread extends Thread {
    private String charSet = System.getProperty("file.encoding");

    private static final int REQ_CODE_INVOKE_SP = 0x01;
    private static final int REQ_CODE_RESULT = 0x02;
    private static final int REQ_CODE_ERROR = 0x04;
    private static final int REQ_CODE_INTERNAL_JDBC = 0x08;
    private static final int REQ_CODE_DESTROY = 0x10;
    private static final int REQ_CODE_END = 0x20;

    private static final int REQ_CODE_UTIL_PING = 0xDE;
    private static final int REQ_CODE_UTIL_STATUS = 0xEE;
    private static final int REQ_CODE_UTIL_TERMINATE_THREAD = 0xFE;
    private static final int REQ_CODE_UTIL_TERMINATE_SERVER = 0xFF; // to shutdown javasp server

    private Socket client;
    private CUBRIDConnectionDefault connection = null;
    private String threadName = null;

    private DataInputStream input;
    private DataOutputStream output;

    /* TODO: It will be replaced with DirectByteBuffer-based new buffer which dynamically extended if overflow exists */
    /* Since DirectByteBuffer's allocation time is slow, DirectByteBuffer pooling should be implemented */
    private ByteBuffer buffer;
    private CUBRIDPacker packer;
    private CUBRIDUnpacker unpacker;

    private AtomicInteger status = new AtomicInteger(ExecuteThreadStatus.IDLE.getValue());

    private StoredProcedure storedProcedure = null;

    ExecuteThread(Socket client) throws IOException {
        super();
        this.client = client;
        output = new DataOutputStream(new BufferedOutputStream(this.client.getOutputStream()));
        buffer = ByteBuffer.allocate(1024);
        packer = new CUBRIDPacker(buffer);
        unpacker = new CUBRIDUnpacker(buffer);
    }

    public Socket getSocket() {
        return client;
    }

    public void closeJdbcConnection() throws IOException, SQLException {
        if (connection != null && compareStatus(ExecuteThreadStatus.CALL)) {
            connection.close();
            setStatus(ExecuteThreadStatus.INVOKE);
        }
    }

    public void closeSocket() {
        try {
            output.close();
            client.close();
        } catch (IOException e) {
        }

        client = null;
        output = null;
        connection = null;
        charSet = null;
    }

    public void setJdbcConnection(Connection con) {
        this.connection = (CUBRIDConnectionDefault) con;
    }

    public Connection getJdbcConnection() {
        return this.connection;
    }

    public void setStatus(Integer value) {
        this.status.set(value);
    }

    public void setStatus(ExecuteThreadStatus value) {
        this.status.set(value.getValue());
    }

    public Integer getStatus() {
        return status.get();
    }

    public boolean compareStatus(ExecuteThreadStatus value) {
        return (status.get() == value.getValue());
    }

    public void setCharSet(String conCharsetName) {
        this.charSet = conCharsetName;
    }

    public void run() {
        /* main routine handling stored procedure */
        int requestCode = -1;
        while (!Thread.interrupted()) {
            try {
                requestCode = listenCommand();
                switch (requestCode) {
                        /* the following two request codes are for processing java stored procedure routine */
                    case REQ_CODE_INVOKE_SP:
                        {
                            processStoredProcedure();
                            break;
                        }
                    case REQ_CODE_DESTROY:
                        {
                            destroyJDBCResources();
                            Thread.currentThread().interrupt();
                            break;
                        }

                        /* the following request codes are for javasp utility */
                    case REQ_CODE_UTIL_PING:
                        {
                            String ping = Server.getServerName();
                            output.writeInt(ping.length());
                            output.writeBytes(ping);
                            output.flush();
                            break;
                        }
                    case REQ_CODE_UTIL_STATUS:
                        {
                            String dbName = Server.getServerName();
                            List<String> vm_args = Server.getJVMArguments();
                            int length = DataUtilities.getLengthtoSend(dbName) + 12;
                            for (String arg : vm_args) {
                                length += DataUtilities.getLengthtoSend(arg) + 4;
                            }
                            output.writeInt(length);
                            output.writeInt(Server.getServerPort());
                            DataUtilities.packAndSendRawString(dbName, output);

                            output.writeInt(vm_args.size());
                            for (String arg : vm_args) {
                                DataUtilities.packAndSendRawString(arg, output);
                            }
                            output.flush();
                            break;
                        }
                    case REQ_CODE_UTIL_TERMINATE_THREAD:
                        {
                            Thread.currentThread().interrupt();
                            break;
                        }
                    case REQ_CODE_UTIL_TERMINATE_SERVER:
                        {
                            Server.stop(0);
                            break;
                        }

                        /* invalid request */
                    default:
                        {
                            // throw new ExecuteException ("invalid request code: " + requestCode);
                        }
                }
            } catch (Throwable e) {
                if (e instanceof IOException) {
                    setStatus(ExecuteThreadStatus.END);
                    /*
                     * CAS disconnects socket
                     * 1) end of the procedure successfully by calling jsp_close_internal_connection ()
                     * 2) socket is in invalid status. we do not have to deal with it here.
                     */
                    break;
                } else {
                    try {
                        closeJdbcConnection();
                    } catch (Exception e2) {
                    }
                    setStatus(ExecuteThreadStatus.ERROR);
                    Throwable throwable = e;
                    if (e instanceof InvocationTargetException) {
                        throwable = ((InvocationTargetException) e).getTargetException();
                    }
                    Server.log(throwable);
                    try {
                        sendError(throwable.toString(), client);
                    } catch (IOException e1) {
                        Server.log(e1);
                    }
                }
            }
        }
        closeSocket();
    }

    private int listenCommand() throws Exception {
        input = new DataInputStream(new BufferedInputStream(this.client.getInputStream()));
        setStatus(ExecuteThreadStatus.IDLE);
        return input.readInt();
    }

    private void processStoredProcedure() throws Exception {
        setStatus(ExecuteThreadStatus.PARSE);

        /* read buffer */
        int size = input.readInt();
        byte[] bytes = new byte[size];
        input.readFully(bytes);

        buffer.clear(); // always clear
        buffer.put(bytes);
        buffer.flip(); /* prepare to read */

        StoredProcedure procedure = makeStoredProcedure();
        Method m = procedure.getTarget().getMethod();

        if (threadName == null || threadName.equalsIgnoreCase(m.getName())) {
            threadName = m.getName();
            Thread.currentThread().setName(threadName);
        }

        Object[] resolved = procedure.checkArgs(procedure.getArgs());

        setStatus(ExecuteThreadStatus.INVOKE);
        Object result = m.invoke(null, resolved);

        /* close server-side JDBC connection */
        closeJdbcConnection();

        /* send results */
        setStatus(ExecuteThreadStatus.RESULT);
        Value resolvedResult = procedure.makeReturnValue(result);
        sendResult(resolvedResult, procedure);

        setStatus(ExecuteThreadStatus.IDLE);
    }

    private StoredProcedure makeStoredProcedure() throws Exception {
        String methodSig = unpacker.unpackCString();
        int paramCount = unpacker.unpackInt();
        Value[] args = readArguments(unpacker, paramCount);
        int returnType = unpacker.unpackInt();

        storedProcedure = new StoredProcedure(methodSig, args, returnType);
        return storedProcedure;
    }

    private void destroyJDBCResources() throws SQLException, IOException {
        setStatus(ExecuteThreadStatus.DESTROY);

        if (connection != null) {
            output.writeInt(REQ_CODE_DESTROY);
            output.flush();
            connection.destroy();
            connection = null;
        } else {
            output.writeInt(REQ_CODE_END);
            output.flush();
        }
    }

    private void returnOutArgs(StoredProcedure sp, CUBRIDPacker packer)
            throws IOException, ExecuteException, TypeMismatchException {
        Value[] args = sp.getArgs();
        for (int i = 0; i < args.length; i++) {
            if (args[i].getMode() > Value.IN) {
                Value v = sp.makeOutValue(args[i].getResolved());
                packer.packValue(
                        ValueUtilities.resolveValue(args[i].getDbType(), v),
                        args[i].getDbType(),
                        this.charSet);
            }
        }
    }

    private void sendResult(Value result, StoredProcedure procedure)
            throws IOException, ExecuteException, TypeMismatchException {
        Object resolvedResult = null;
        if (result != null) {
            resolvedResult = ValueUtilities.resolveValue(procedure.getReturnType(), result);
        }

        buffer.flip(); /* prepare to put */
        packer.packValue(resolvedResult, procedure.getReturnType(), this.charSet);
        returnOutArgs(procedure, packer);

        output.writeInt(REQ_CODE_RESULT);
        output.writeInt(buffer.position() + 4);
        output.write(buffer.array(), 0, buffer.position());
        output.writeInt(REQ_CODE_RESULT);
        output.flush();
    }

    public void sendCall() throws IOException {
        if (compareStatus(ExecuteThreadStatus.INVOKE)) {
            setStatus(ExecuteThreadStatus.CALL);
            output.writeInt(REQ_CODE_INTERNAL_JDBC);
            output.flush();
        }
    }

    private void sendError(String exception, Socket socket) throws IOException {

        buffer.flip(); /* prepare to put */
        packer.packValue(new Integer(1), DBType.DB_INT, this.charSet);
        packer.packValue(exception, DBType.DB_STRING, this.charSet);

        output.writeInt(REQ_CODE_ERROR);
        output.writeInt(buffer.position() + 4);
        output.write(buffer.array(), 0, buffer.position());
        output.writeInt(REQ_CODE_ERROR);
        output.flush();
    }

    private Value[] readArguments(CUBRIDUnpacker u, int paramCount) throws TypeMismatchException {
        Value[] args = new Value[paramCount];

        for (int i = 0; i < paramCount; i++) {
            int mode = u.unpackInt();
            int dbType = u.unpackInt();
            int paramType = u.unpackInt();
            int paramSize = u.unpackInt();

            Value arg = u.unpackValue(paramSize, paramType, mode, dbType);
            args[i] = (arg);
        }

        return args;
    }
}
