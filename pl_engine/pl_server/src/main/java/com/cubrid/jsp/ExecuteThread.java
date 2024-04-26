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

import com.cubrid.jsp.classloader.ClassLoaderManager;
import com.cubrid.jsp.compiler.CompiledCode;
import com.cubrid.jsp.compiler.MemoryJavaCompiler;
import com.cubrid.jsp.compiler.SourceCode;
import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.context.ContextManager;
import com.cubrid.jsp.data.AuthInfo;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.data.CompileInfo;
import com.cubrid.jsp.data.DataUtilities;
import com.cubrid.jsp.exception.ExecuteException;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.protocol.Header;
import com.cubrid.jsp.protocol.PrepareArgs;
import com.cubrid.jsp.protocol.RequestCode;
import com.cubrid.jsp.value.NullValue;
import com.cubrid.jsp.value.StringValue;
import com.cubrid.jsp.value.Value;
import com.cubrid.jsp.value.ValueUtilities;
import com.cubrid.plcsql.compiler.PlcsqlCompilerMain;
import com.cubrid.plcsql.predefined.PlcsqlRuntimeError;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.OutputStream;
import java.lang.reflect.InvocationTargetException;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.SQLException;
import java.util.List;
import org.apache.commons.compress.archivers.jar.JarArchiveEntry;
import org.apache.commons.compress.archivers.jar.JarArchiveOutputStream;
import org.apache.commons.io.IOUtils;

public class ExecuteThread extends Thread {

    public static String charSet = "UTF-8";

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
                    case RequestCode.PREPARE_ARGS:
                        {
                            processPrepare();
                            break;
                        }
                    case RequestCode.INVOKE_SP:
                        {
                            processStoredProcedure();
                            ctx = null;
                            break;
                        }

                    case RequestCode.COMPILE:
                        {
                            processCompile();
                            break;
                        }

                        /* the following request codes are for javasp utility */
                    case RequestCode.UTIL_PING:
                        {
                            String ping = Server.getServer().getServerName();

                            resultBuffer.clear(); /* prepare to put */
                            packer.setBuffer(resultBuffer);
                            packer.packString(ping);

                            resultBuffer = packer.getBuffer();
                            writeBuffer(resultBuffer);
                            break;
                        }
                    case RequestCode.UTIL_STATUS:
                        {
                            // TODO: create a packable class for status
                            resultBuffer.clear(); /* prepare to put */
                            packer.setBuffer(resultBuffer);

                            packer.packInt(Server.getServer().getServerPort());
                            packer.packString(Server.getServer().getServerName());
                            List<String> vm_args = Server.getJVMArguments();
                            packer.packInt(vm_args.size());
                            for (String arg : vm_args) {
                                packer.packString(arg);
                            }

                            resultBuffer = packer.getBuffer();
                            writeBuffer(resultBuffer);
                            break;
                        }
                    case RequestCode.UTIL_TERMINATE_THREAD:
                        {
                            // hacky way.. If thread is terminated and socket is closed immediately,
                            // "ping" or "status" command does not work properly
                            sleep(100);
                            Thread.currentThread().interrupt();
                            break;
                        }
                    case RequestCode.UTIL_TERMINATE_SERVER:
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
                        // TODO: error managing module
                        if (throwable instanceof SQLException) {
                            sendError(throwable.toString());
                        } else if (throwable instanceof PlcsqlRuntimeError) {
                            PlcsqlRuntimeError plcsqlError = (PlcsqlRuntimeError) throwable;
                            String errMsg =
                                    String.format(
                                            "\n  (line %d, column %d) %s",
                                            plcsqlError.getLine(),
                                            plcsqlError.getColumn(),
                                            plcsqlError.getMessage());
                            sendError(errMsg);
                        } else {
                            sendError(throwable.toString());
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
        ctx.checkTranId(prepareArgs.getTranId());
    }

    private void processStoredProcedure() throws Exception {
        unpacker.setBuffer(ctx.getInboundQueue().take());
        long id = unpacker.unpackBigint();
        int tid = unpacker.unpackInt();

        ctx.checkTranId(tid);

        StoredProcedure procedure = makeStoredProcedure(unpacker);

        pushUser(procedure.getAuthUser());
        Value result = procedure.invoke();
        popUser();

        /* send results */
        sendResult(result, procedure);
    }

    private void pushUser(String user) throws Exception {
        sendAuthCommand(0, user);
    }

    private void popUser() throws Exception {
        sendAuthCommand(1, "");
    }

    private void writeJar(List<CompiledCode> compiledCodeList, Path jarPath) throws IOException {
        JarArchiveOutputStream jaos = null;
        try {
            OutputStream jarStream = Files.newOutputStream(jarPath);
            jaos = new JarArchiveOutputStream(new BufferedOutputStream(jarStream));

            for (CompiledCode c : compiledCodeList) {
                JarArchiveEntry jae = new JarArchiveEntry(c.getClassName() + ".class");
                jaos.putArchiveEntry(jae);
                ByteArrayInputStream bis = new ByteArrayInputStream(c.getByteCode());
                IOUtils.copy(bis, jaos);
                bis.close();
                jaos.closeArchiveEntry();
            }
        } catch (IOException e) {
            throw e;
        } finally {
            if (jaos != null) {
                jaos.flush();
                jaos.finish();
                jaos.close();
            }
        }
    }

    private void processCompile() throws Exception {
        unpacker.setBuffer(ctx.getInboundQueue().take());
        boolean verbose = unpacker.unpackBool();
        String inSource = unpacker.unpackCString();

        CompileInfo info = null;
        try {
            info = PlcsqlCompilerMain.compilePLCSQL(inSource, verbose);
            if (info.errCode == 0) {

                // The following writes .java file into /dynamic directory
                Path javaFilePath =
                        ClassLoaderManager.getDynamicPath().resolve(info.className + ".java");
                File file = javaFilePath.toFile();
                if (file.exists()) {
                    file.delete();
                }
                new FileWriter(file).append(info.translated).close();
                //

                MemoryJavaCompiler compiler = new MemoryJavaCompiler();
                SourceCode sCode = new SourceCode(info.className, info.translated);
                compiler.compile(sCode);

                Path jarPath = ClassLoaderManager.getDynamicPath().resolve(info.className + ".jar");
                List<CompiledCode> codeList = compiler.getFileManager().getCodeList();
                writeJar(codeList, jarPath);
            }
        } catch (Exception e) {
            info =
                    new CompileInfo(
                            -1, 0, 0, e.getMessage().isEmpty() ? "unknown error" : e.getMessage());
            throw new RuntimeException(e);
        } finally {
            CUBRIDPacker packer = new CUBRIDPacker(ByteBuffer.allocate(1024));
            info.pack(packer);
            Context.getCurrentExecuteThread().sendCommand(RequestCode.COMPILE, packer.getBuffer());
        }
    }

    private StoredProcedure makeStoredProcedure(CUBRIDUnpacker unpacker) throws Exception {
        String methodSig = unpacker.unpackCString();
        String authUser = unpacker.unpackCString();
        int paramCount = unpacker.unpackInt();

        Value[] arguments = prepareArgs.getArgs();
        Value[] methodArgs = new Value[paramCount];
        for (int i = 0; i < paramCount; i++) {
            int pos = unpacker.unpackInt();
            int mode = unpacker.unpackInt();
            int type = unpacker.unpackInt();
            int defaultValSize = unpacker.unpackInt();
            String defaultVal = null;

            Value val = null;
            if (pos != -1) {
                val = arguments[pos];
            } else {
                if (defaultValSize > 0) {
                    defaultVal = unpacker.unpackCString();
                    val = new StringValue(defaultVal);
                } else if (defaultValSize == 0) {
                    val = new StringValue("");
                } else if (defaultValSize == -2) {
                    val = new NullValue();
                } else {
                    // internal error
                    assert false;
                }
            }
            val.setMode(mode);
            val.setDbType(type);

            methodArgs[i] = val;
        }
        int returnType = unpacker.unpackInt();

        boolean transactionControl = unpacker.unpackBool();
        getCurrentContext().setTransactionControl(transactionControl);

        storedProcedure = new StoredProcedure(methodSig, authUser, methodArgs, returnType);
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

        packer.packInt(RequestCode.RESULT);
        packer.align(DataUtilities.MAX_ALIGNMENT);
        packer.packValue(resolvedResult, procedure.getReturnType(), this.charSet);
        returnOutArgs(procedure, packer);

        resultBuffer = packer.getBuffer();
        writeBuffer(resultBuffer);
    }

    public void sendCommand(int code, ByteBuffer buffer) throws IOException {
        resultBuffer.clear(); /* prepare to put */
        packer.setBuffer(resultBuffer);

        packer.packInt(code);
        packer.align(DataUtilities.MAX_ALIGNMENT);
        packer.packPrimitiveBytes(buffer);

        resultBuffer = packer.getBuffer();
        writeBuffer(resultBuffer);
    }

    public void sendCommand(ByteBuffer buffer) throws IOException {
        resultBuffer.clear(); /* prepare to put */
        packer.setBuffer(resultBuffer);

        packer.packInt(RequestCode.INTERNAL_JDBC);
        packer.align(DataUtilities.MAX_ALIGNMENT);
        packer.packPrimitiveBytes(buffer);

        resultBuffer = packer.getBuffer();
        writeBuffer(resultBuffer);
    }

    private void sendError(String exception) throws IOException {
        resultBuffer.clear();
        packer.setBuffer(resultBuffer);

        packer.packInt(RequestCode.ERROR);
        packer.align(DataUtilities.MAX_ALIGNMENT);
        packer.packString(exception);

        resultBuffer = packer.getBuffer();
        writeBuffer(resultBuffer);
    }

    private void sendAuthCommand(int command, String authName) throws Exception {
        AuthInfo info = new AuthInfo(command, authName);
        CUBRIDPacker packer = new CUBRIDPacker(ByteBuffer.allocate(128));
        packer.packInt(RequestCode.REQUEST_CHANGE_AUTH_RIGHTS);
        info.pack(packer);
        Context.getCurrentExecuteThread().sendCommand(packer.getBuffer());

        ByteBuffer responseBuffer = Context.getCurrentExecuteThread().receiveBuffer();
        CUBRIDUnpacker unpacker = new CUBRIDUnpacker(responseBuffer);
        /* read header, dummy */
        Header header = new Header(unpacker);
        ByteBuffer payload = unpacker.unpackBuffer();
        unpacker.setBuffer(payload);
        int responseCode = unpacker.unpackInt();
    }
}
