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

import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.context.ContextManager;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.data.CompileInfo;
import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.data.DataUtilities;
import com.cubrid.jsp.exception.ExecuteException;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.protocol.Header;
import com.cubrid.jsp.protocol.PrepareArgs;
import com.cubrid.jsp.value.Value;
import com.cubrid.jsp.value.ValueUtilities;
import com.cubrid.plcsql.handler.TestMain;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.charset.Charset;
import java.sql.SQLException;
import java.util.List;

public class ExecuteThread extends Thread {

    public static String charSet = "UTF-8";

    private static final int REQ_CODE_INVOKE_SP = 0x01;
    private static final int REQ_CODE_RESULT = 0x02;
    private static final int REQ_CODE_ERROR = 0x04;
    private static final int REQ_CODE_INTERNAL_JDBC = 0x08;

    // private static final int REQ_CODE_DESTROY = 0x10;
    // private static final int REQ_CODE_END = 0x20;

    private static final int REQ_CODE_PREPARE_ARGS = 0x40;

    private static final int REQ_CODE_COMPILE = 0x80;

    private static final int REQ_CODE_UTIL_PING = 0xDE;
    private static final int REQ_CODE_UTIL_STATUS = 0xEE;
    private static final int REQ_CODE_UTIL_TERMINATE_THREAD = 0xFE;
    private static final int REQ_CODE_UTIL_TERMINATE_SERVER = 0xFF; // to shutdown javasp server

    private Socket client;

    private DataInputStream input;
    private DataOutputStream output;

    /*
     * TODO: It will be replaced with DirectByteBuffer-based new buffer which
     * dynamically extended if overflow exists
     */
    /*
     * Since DirectByteBuffer's allocation time is slow, DirectByteBuffer pooling
     * should be implemented
     */
    private ByteBuffer resultBuffer;

    private CUBRIDUnpacker unpacker = new CUBRIDUnpacker();
    private CUBRIDPacker packer;

    private StoredProcedure storedProcedure = null;
    private PrepareArgs prepareArgs = null;

    private Context ctx = null;

    ExecuteThread(Socket client) throws IOException {
        super();
        this.client = client;
        output = new DataOutputStream(new BufferedOutputStream(this.client.getOutputStream()));

        resultBuffer = ByteBuffer.allocate(4096);

        packer = new CUBRIDPacker(resultBuffer);
    }

    public Socket getSocket() {
        return client;
    }

    public Context getCurrentContext() {
        return ctx;
    }

    public void closeSocket() {
        try {
            output.close();
            client.close();
        } catch (IOException e) {
        }

        client = null;
        output = null;
        // charSet = null;
    }

    public void setCharSet(String conCharsetName) {
        // this.charSet = conCharsetName;
    }

    @Override
    public void run() {
        /* main routine handling stored procedure */
        Header header = null;
        while (!Thread.interrupted()) {
            try {
                header = listenCommand();
                ContextManager.registerThread(Thread.currentThread().getId(), ctx.getSessionId());
                switch (header.code) {
                        /*
                         * the following two request codes are for processing java stored procedure
                         * routine
                         */
                    case REQ_CODE_PREPARE_ARGS:
                        {
                            processPrepare();
                            break;
                        }
                    case REQ_CODE_INVOKE_SP:
                        {
                            processStoredProcedure();
                            ctx = null;
                            break;
                        }

                    case REQ_CODE_COMPILE:
                        {
                            unpacker.setBuffer(ctx.getInboundQueue().take());
                            boolean verbose = unpacker.unpackBool();
                            String inSource = unpacker.unpackCString();

                            try {
                                CompileInfo info = TestMain.compilePLCSQL(inSource, verbose);

                                resultBuffer.clear(); /* prepare to put */
                                packer.setBuffer(resultBuffer);
                                packer.packString(info.translated);
                                packer.packString(info.sqlTemplate);

                                String javaFilePath =
                                        StoredProcedureClassLoader.ROOT_PATH
                                                + info.className
                                                + ".java";
                                File file = new File(javaFilePath);
                                FileOutputStream fos = new FileOutputStream(file, false);
                                fos.write(info.translated.getBytes(Charset.forName("UTF-8")));
                                fos.close();

                                String cubrid_env_root = Server.getRootPath();
                                String command =
                                        "javac "
                                                + javaFilePath
                                                + " -cp "
                                                + cubrid_env_root
                                                + "/java/splib.jar";

                                Process proc = Runtime.getRuntime().exec(command);
                                proc.getErrorStream().close();
                                proc.getInputStream().close();
                                proc.getOutputStream().close();
                                proc.waitFor();

                                if (proc.exitValue() != 0) {
                                    // TODO
                                    throw new RuntimeException(command);
                                }

                                resultBuffer = packer.getBuffer();

                                output.writeInt(resultBuffer.position());
                                output.write(resultBuffer.array(), 0, resultBuffer.position());
                                output.flush();
                            } catch (Exception e) {
                                output.writeInt(0);
                                output.flush();
                                throw new RuntimeException(e);
                            }
                            break;
                        }

                        /* the following request codes are for javasp utility */
                    case REQ_CODE_UTIL_PING:
                        {
                            String ping = Server.getServerName();

                            resultBuffer.clear(); /* prepare to put */
                            packer.setBuffer(resultBuffer);
                            packer.packString(ping);

                            resultBuffer = packer.getBuffer();
                            writeBuffer(resultBuffer);
                            break;
                        }
                    case REQ_CODE_UTIL_STATUS:
                        {
                            // TODO: create a packable class for status
                            resultBuffer.clear(); /* prepare to put */
                            packer.setBuffer(resultBuffer);

                            packer.packInt(Server.getServerPort());
                            packer.packString(Server.getServerName());
                            List<String> vm_args = Server.getJVMArguments();
                            packer.packInt(vm_args.size());
                            for (String arg : vm_args) {
                                packer.packString(arg);
                            }

                            resultBuffer = packer.getBuffer();
                            writeBuffer(resultBuffer);
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
                ContextManager.deregisterThread(Thread.currentThread().getId());
            } catch (Throwable e) {
                if (e instanceof IOException) {
                    /*
                     * CAS disconnects socket
                     * 1) end of the procedure successfully by calling jsp_close_internal_connection
                     * 2) socket is in invalid status. we do not have to deal with it here.
                     */
                    break;
                } else {
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
            } finally {
                ContextManager.deregisterThread(Thread.currentThread().getId());
                ctx = null;
            }
        }
        closeSocket();
    }

    private Header listenCommand() throws Exception {
        ByteBuffer inputBuffer = receiveBuffer();

        unpacker.setBuffer(inputBuffer);

        /* read header */
        Header header = new Header(unpacker);
        ctx = ContextManager.getContext(header.id);
        ctx.checkHeader(header);

        ByteBuffer payloadBuffer =
                ByteBuffer.wrap(
                        inputBuffer.array(),
                        unpacker.getCurrentPosition(),
                        unpacker.getCurrentLimit() - unpacker.getCurrentPosition());
        ctx.getInboundQueue().add(payloadBuffer);
        return header;
    }

    public ByteBuffer receiveBuffer() throws IOException {
        if (input == null) {
            input = new DataInputStream(new BufferedInputStream(this.client.getInputStream()));
        }

        int size = input.readInt(); // size
        byte[] bytes = new byte[size];
        input.readFully(bytes);

        return ByteBuffer.wrap(bytes);
    }

    private void writeBuffer(ByteBuffer buffer) throws IOException {
        output.writeInt(buffer.position());
        output.write(buffer.array(), 0, buffer.position());
        output.flush();
    }

    public CUBRIDUnpacker getUnpacker() {
        return unpacker;
    }

    private void processPrepare() throws Exception {
        unpacker.setBuffer(ctx.getInboundQueue().take());
        if (prepareArgs == null) {
            prepareArgs = new PrepareArgs(unpacker);
        } else {
            prepareArgs.readArgs(unpacker);
        }
    }

    private void processStoredProcedure() throws Exception {
        unpacker.setBuffer(ctx.getInboundQueue().take());
        long id = unpacker.unpackBigint();

        StoredProcedure procedure = makeStoredProcedure(unpacker);
        Value result = procedure.invoke();

        /* send results */
        sendResult(result, procedure);
    }

    private StoredProcedure makeStoredProcedure(CUBRIDUnpacker unpacker) throws Exception {
        String methodSig = unpacker.unpackCString();
        int paramCount = unpacker.unpackInt();

        Value[] arguments = prepareArgs.getArgs();
        Value[] methodArgs = new Value[paramCount];
        for (int i = 0; i < paramCount; i++) {
            int pos = unpacker.unpackInt();
            int mode = unpacker.unpackInt();
            int type = unpacker.unpackInt();

            Value val = arguments[pos];
            val.setMode(mode);
            val.setDbType(type);

            methodArgs[i] = val;
        }
        int returnType = unpacker.unpackInt();

        storedProcedure = new StoredProcedure(methodSig, methodArgs, returnType);
        return storedProcedure;
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

        packer.packInt(REQ_CODE_RESULT);
        packer.align(DataUtilities.MAX_ALIGNMENT);
        packer.packValue(resolvedResult, procedure.getReturnType(), this.charSet);
        returnOutArgs(procedure, packer);

        resultBuffer = packer.getBuffer();
        writeBuffer(resultBuffer);
    }

    public void sendCommand(ByteBuffer buffer) throws IOException {
        resultBuffer.clear(); /* prepare to put */
        packer.setBuffer(resultBuffer);

        packer.packInt(REQ_CODE_INTERNAL_JDBC);
        packer.align(DataUtilities.MAX_ALIGNMENT);
        packer.packPrimitiveBytes(buffer);

        resultBuffer = packer.getBuffer();
        writeBuffer(resultBuffer);
    }

    private void sendError(String exception, Socket socket) throws IOException {
        resultBuffer.clear();
        packer.setBuffer(resultBuffer);

        packer.packInt(REQ_CODE_ERROR);
        packer.align(DataUtilities.MAX_ALIGNMENT);
        packer.packValue(new Integer(1), DBType.DB_INT, this.charSet);
        packer.packValue(exception, DBType.DB_STRING, this.charSet);

        resultBuffer = packer.getBuffer();
        writeBuffer(resultBuffer);
    }
}
