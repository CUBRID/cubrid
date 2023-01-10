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
import com.cubrid.jsp.jdbc.CUBRIDServerSideConnection;
import com.cubrid.jsp.value.Value;
import com.cubrid.jsp.value.ValueUtilities;
import com.cubrid.plcsql.handler.TestMain;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

public class ExecuteThread extends Thread {

    // TODO: get charset from DB Server
    public static String charSet = "UTF-8"; // System.getProperty("file.encoding");

    private static final int REQ_CODE_INVOKE_SP = 0x01;
    private static final int REQ_CODE_RESULT = 0x02;
    private static final int REQ_CODE_ERROR = 0x04;
    private static final int REQ_CODE_INTERNAL_JDBC = 0x08;
    private static final int REQ_CODE_DESTROY = 0x10;
    private static final int REQ_CODE_END = 0x20;
    private static final int REQ_CODE_PREPARE_ARGS = 0x40;

    private static final int REQ_CODE_COMPILE = 0x80;

    private static final int REQ_CODE_UTIL_PING = 0xDE;
    private static final int REQ_CODE_UTIL_STATUS = 0xEE;
    private static final int REQ_CODE_UTIL_TERMINATE_THREAD = 0xFE;
    private static final int REQ_CODE_UTIL_TERMINATE_SERVER = 0xFF; // to shutdown javasp server

    private long id;
    private Socket client;
    private CUBRIDServerSideConnection connection = null;
    private String threadName = null;

    private DataInputStream input;
    private DataOutputStream output;

    /* TODO: It will be replaced with DirectByteBuffer-based new buffer which dynamically extended if overflow exists */
    /* Since DirectByteBuffer's allocation time is slow, DirectByteBuffer pooling should be implemented */
    private ByteBuffer readbuffer;
    private ByteBuffer resultBuffer;

    private CUBRIDPacker packer;
    private CUBRIDUnpacker unpacker;

    private AtomicInteger status = new AtomicInteger(ExecuteThreadStatus.IDLE.getValue());

    private Value[] arguments = null;
    private StoredProcedure storedProcedure = null;

    ExecuteThread(Socket client) throws IOException {
        super();
        this.client = client;
        output = new DataOutputStream(new BufferedOutputStream(this.client.getOutputStream()));

        readbuffer = ByteBuffer.allocate(4096);
        resultBuffer = ByteBuffer.allocate(4096);

        packer = new CUBRIDPacker(resultBuffer);
        unpacker = new CUBRIDUnpacker(readbuffer);
    }

    public Socket getSocket() {
        return client;
    }

    public void closeJdbcConnection() throws IOException, SQLException {
        if (connection != null) {
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
        // charSet = null;
    }

    public Connection createConnection() {
        if (this.connection == null) {
            this.connection = new CUBRIDServerSideConnection(this);
        }
        return this.connection;
    }

    public void setJdbcConnection(Connection con) throws IOException {
        this.connection = (CUBRIDServerSideConnection) con;
        // sendCommand(null);
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
        // this.charSet = conCharsetName;
    }

    @Override
    public void run() {
        /* main routine handling stored procedure */
        int requestCode = -1;
        while (!Thread.interrupted()) {
            try {
                requestCode = listenCommand();
                switch (requestCode) {
                        /* the following two request codes are for processing java stored procedure routine */
                    case REQ_CODE_PREPARE_ARGS:
                        {
                            processPrepare();
                            break;
                        }
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

                    case REQ_CODE_COMPILE:
                        {
                            id = unpacker.unpackBigint();
                            String inSource = unpacker.unpackCString();

                            try {
                                String outSource = TestMain.compilePLCSQL(inSource);

                                resultBuffer.clear(); /* prepare to put */
                                packer.setBuffer(resultBuffer);
                                packer.packString(outSource);

                                resultBuffer = packer.getBuffer();

                                output.writeInt(resultBuffer.position());
                                output.write(resultBuffer.array(), 0, resultBuffer.position());
                            } catch (Exception e) {
                                output.writeInt(0);
                            }
                            output.flush();
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
                            // hacky way.. If thread is terminated and socket is closed immediately,
                            // "ping" or "status" command does not work properly
                            sleep(100);
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
                        if (throwable instanceof SQLException) {
                            sendError(throwable.getMessage(), client);
                        } else {
                            sendError(throwable.toString(), client);
                        }
                    } catch (IOException e1) {
                        Server.log(e1);
                    }
                }
            }
        }
        closeSocket();
    }

    private int listenCommand() throws Exception {
        receiveBuffer();

        /* read header */
        int command = unpacker.unpackInt();

        setStatus(ExecuteThreadStatus.IDLE);
        return command;
    }

    public CUBRIDUnpacker receiveBuffer() throws IOException {
        if (input == null) {
            input = new DataInputStream(new BufferedInputStream(this.client.getInputStream()));
        }

        int size = input.readInt(); // size
        byte[] bytes = new byte[size];
        input.readFully(bytes);

        ensureReadBufferSpace(size);
        unpacker.setBuffer(readbuffer);

        readbuffer.clear(); // always clear
        readbuffer.put(bytes);
        readbuffer.flip(); /* prepare to read */

        return unpacker;
    }

    private void processPrepare() throws Exception {
        id = unpacker.unpackBigint();

        int argCount = unpacker.unpackInt();
        if (arguments == null || argCount != arguments.length) {
            arguments = new Value[argCount];
        }

        readArguments(unpacker, arguments);
    }

    private void processStoredProcedure() throws Exception {
        id = unpacker.unpackBigint();

        setStatus(ExecuteThreadStatus.PARSE);
        StoredProcedure procedure = makeStoredProcedure();

        setStatus(ExecuteThreadStatus.INVOKE);
        Value result = procedure.invoke();

        /* close server-side JDBC connection */
        closeJdbcConnection();

        /* send results */
        setStatus(ExecuteThreadStatus.RESULT);
        sendResult(result, procedure);

        setStatus(ExecuteThreadStatus.IDLE);
    }

    private StoredProcedure makeStoredProcedure() throws Exception {
        String methodSig = unpacker.unpackCString();
        int paramCount = unpacker.unpackInt();

        Value[] methodArgs = new Value[paramCount];
        for (int i = 0; i < paramCount; i++) {
            int pos = unpacker.unpackInt();
            int mode = unpacker.unpackInt();
            int type = unpacker.unpackInt();

            Value val = this.arguments[pos];
            val.setMode(mode);
            val.setDbType(type);

            methodArgs[i] = val;
        }
        int returnType = unpacker.unpackInt();

        storedProcedure = new StoredProcedure(methodSig, methodArgs, returnType);
        return storedProcedure;
    }

    private void destroyJDBCResources() throws SQLException, IOException {
        setStatus(ExecuteThreadStatus.DESTROY);

        if (connection != null) {
            output.writeInt(REQ_CODE_DESTROY);
            output.flush();
            // TODO: connection.destroy();
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

        resultBuffer.clear(); /* prepare to put */
        packer.setBuffer(resultBuffer);

        packer.packValue(resolvedResult, procedure.getReturnType(), this.charSet);
        returnOutArgs(procedure, packer);
        resultBuffer = packer.getBuffer();

        output.writeInt(REQ_CODE_RESULT);
        output.writeInt(resultBuffer.position());
        output.write(resultBuffer.array(), 0, resultBuffer.position());
        output.flush();
    }

    public void sendCommand(ByteBuffer buffer) throws IOException {
        output.writeInt(REQ_CODE_INTERNAL_JDBC);
        if (buffer != null) {
            output.writeInt(buffer.position());
            output.write(buffer.array(), 0, buffer.position());
        }
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
        resultBuffer.clear();
        packer.setBuffer(resultBuffer);

        packer.packValue(new Integer(1), DBType.DB_INT, this.charSet);
        packer.packValue(exception, DBType.DB_STRING, this.charSet);

        resultBuffer = packer.getBuffer();

        output.writeInt(REQ_CODE_ERROR);
        output.writeInt(resultBuffer.position());
        output.write(resultBuffer.array(), 0, resultBuffer.position());
        output.flush();
    }

    private Value[] readArguments(CUBRIDUnpacker u, Value[] args) throws TypeMismatchException {

        for (int i = 0; i < args.length; i++) {
            int paramType = u.unpackInt();

            Value arg = u.unpackValue(paramType);
            args[i] = (arg);
        }

        return args;
    }

    private Value[] readArguments(CUBRIDUnpacker u, int paramCount) throws TypeMismatchException {
        Value[] args = new Value[paramCount];

        for (int i = 0; i < paramCount; i++) {
            int mode = u.unpackInt();
            int dbType = u.unpackInt();
            int paramType = u.unpackInt();

            Value arg = u.unpackValue(paramType, mode, dbType);
            args[i] = (arg);
        }

        return args;
    }

    private static final int EXPAND_FACTOR = 2;

    private void ensureReadBufferSpace(int size) {
        if (readbuffer.capacity() > size) {
            return;
        }
        int newCapacity = (int) (readbuffer.capacity() * EXPAND_FACTOR);
        while (newCapacity < (readbuffer.capacity() + size)) {
            newCapacity *= EXPAND_FACTOR;
        }
        ByteBuffer expanded = ByteBuffer.allocate(newCapacity);
        expanded.order(readbuffer.order());
        expanded.put(readbuffer);
        readbuffer = expanded;
    }
}
