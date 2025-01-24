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
import com.cubrid.jsp.code.CompiledCode;
import com.cubrid.jsp.code.CompiledCodeSet;
import com.cubrid.jsp.code.SourceCode;
import com.cubrid.jsp.compiler.MemoryJavaCompiler;
import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.context.ContextManager;
import com.cubrid.jsp.data.AuthInfo;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.data.CompileInfo;
import com.cubrid.jsp.data.CompileRequest;
import com.cubrid.jsp.data.DataUtilities;
import com.cubrid.jsp.exception.ExecuteException;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.protocol.BootstrapRequest;
import com.cubrid.jsp.protocol.Header;
import com.cubrid.jsp.protocol.PrepareArgs;
import com.cubrid.jsp.protocol.RequestCode;
import com.cubrid.jsp.value.Value;
import com.cubrid.plcsql.compiler.PlcsqlCompilerMain;
import com.cubrid.plcsql.predefined.PlcsqlRuntimeError;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.lang.reflect.InvocationTargetException;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.sql.SQLException;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import org.apache.commons.compress.archivers.jar.JarArchiveEntry;
import org.apache.commons.compress.archivers.jar.JarArchiveOutputStream;

public class ExecuteThread extends Thread {
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

                    case RequestCode.DESTROY:
                        {
                            ContextManager.destroyContext(ctx.getSessionId());
                            break;
                        }

                        /* the following request codes are for system requests */
                    case RequestCode.UTIL_BOOTSTRAP:
                        {
                            processBootstrap();
                            break;
                        }
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
                            String msg = throwable.getMessage();
                            if (msg == null) {
                                msg = "Unexpected sql error";
                            }
                            sendError(msg);
                        } else if (throwable instanceof PlcsqlRuntimeError) {
                            PlcsqlRuntimeError plcsqlError = (PlcsqlRuntimeError) throwable;
                            int line = plcsqlError.getLine();
                            int col = plcsqlError.getColumn();
                            String errMsg;
                            if (line == -1 && col == -1) {
                                // exception was thrown not in the SP code but in the PL engine code
                                errMsg = String.format("\n  %s", plcsqlError.getMessage());
                            } else {
                                errMsg =
                                        String.format(
                                                "\n  (line %d, column %d) %s",
                                                line, col, plcsqlError.getMessage());
                            }
                            sendError(errMsg);
                        } else {
                            String msg = throwable.getMessage();
                            if (msg == null) {
                                msg = "Unexpected internal error";
                            }
                            sendError(msg);
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

        int startOffset = unpacker.getCurrentPosition();
        int payloadSize = unpacker.getCurrentLimit() - startOffset;
        if (payloadSize > 0) {
            ByteBuffer payloadBuffer =
                    ByteBuffer.wrap(inputBuffer.array(), startOffset, payloadSize);

            ctx.getInboundQueue().add(payloadBuffer);
        }

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

    private void readSessionParameter(CUBRIDUnpacker unpacker) {
        int paramCnt = (int) unpacker.unpackBigint();
        if (paramCnt > 0) {
            for (int i = 0; i < paramCnt; i++) {
                SysParam sysParam = new SysParam(unpacker);
                ctx.getSystemParameters().put(sysParam.getParamId(), sysParam);
            }
        }
    }

    private void processStoredProcedure() throws Exception {
        unpacker.setBuffer(ctx.getInboundQueue().take());

        // session parameters
        readSessionParameter(unpacker);

        // prepare
        if (prepareArgs == null) {
            prepareArgs = new PrepareArgs(unpacker);
        } else {
            prepareArgs.readArgs(unpacker);
        }

        long id = unpacker.unpackBigint();
        int tid = unpacker.unpackInt();

        ctx.checkTranId(tid);

        StoredProcedure procedure = makeStoredProcedure(unpacker);

        Value result = procedure.invoke();

        /* send results */
        sendResult(result, procedure);
    }

    private void writeJar(CompiledCodeSet codeSet, OutputStream jarStream) throws IOException {
        JarArchiveOutputStream jaos = null;
        try {
            jaos = new JarArchiveOutputStream(new BufferedOutputStream(jarStream));

            for (Map.Entry<String, CompiledCode> entry : codeSet.getCodeList()) {
                JarArchiveEntry jae =
                        new JarArchiveEntry(entry.getValue().getClassNameWithExtention());
                byte[] arr = entry.getValue().getByteCode();
                jae.setSize(arr.length);
                jaos.putArchiveEntry(jae);
                jaos.write(arr);
                jaos.flush();
                // ByteArrayInputStream bis = new
                // ByteArrayInputStream(entry.getValue().getByteCode());
                // IOUtils.copy(bis, jaos);
                // bis.close();

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

    private void processBootstrap() throws Exception {
        unpacker.setBuffer(ctx.getInboundQueue().take());

        int result = 1; // failed
        try {
            BootstrapRequest request = new BootstrapRequest(unpacker);
            Server.bootstrap(request);
            result = 0; // no error
        } catch (Exception e) {
            // ignore, 1 will be returned
            Server.log(e);
        }

        resultBuffer.clear(); /* prepare to put */
        packer.setBuffer(resultBuffer);
        packer.packInt(result);
        resultBuffer = packer.getBuffer();
        writeBuffer(resultBuffer);
    }

    private void processCompile() throws Exception {
        unpacker.setBuffer(ctx.getInboundQueue().take());

        // session parameters
        readSessionParameter(unpacker);

        CompileRequest request = new CompileRequest(unpacker);

        // TODO: Pass CompileRequest directly to compilePLCSQL ()
        boolean verbose = false;
        if (request.mode.contains("v")) {
            verbose = true;
        }
        String inSource = request.code;
        String owner = request.owner;

        CompileInfo info = null;
        try {
            info = PlcsqlCompilerMain.compilePLCSQL(inSource, owner, verbose);
            if (info.errCode == 0) {
                MemoryJavaCompiler compiler = new MemoryJavaCompiler();
                SourceCode sCode = new SourceCode(info.className, info.translated);

                // dump translated code into $CUBRID_TMP
                if (Context.getSystemParameterBool(SysParam.STORED_PROCEDURE_DUMP_ICODE)) {

                    Path dirPath = Paths.get(Server.getConfig().getTmpPath() + "/icode");
                    if (Files.notExists(dirPath)) {
                        Files.createDirectories(dirPath);
                    }

                    Path path = dirPath.resolve(info.className + ".java");
                    Files.write(path, info.translated.getBytes(Context.getSessionCharset()));
                }

                CompiledCodeSet codeSet = compiler.compile(sCode);

                int mode = 1; // 0: temp file mode, 1: memory stream mode
                byte[] data = null;

                // write to persistent
                if (mode == 0) {
                    Path jarPath =
                            ClassLoaderManager.getDynamicPath().resolve(info.className + ".jar");
                    OutputStream jarStream = Files.newOutputStream(jarPath);
                    writeJar(codeSet, jarStream);
                    data = Files.readAllBytes(jarPath);
                    Files.deleteIfExists(jarPath);
                } else {
                    ByteArrayOutputStream baos = new ByteArrayOutputStream();
                    writeJar(codeSet, baos);
                    data = baos.toByteArray();
                }

                info.compiledType = 1; // TODO: always jar
                info.compiledCode = Base64.getEncoder().encode(data);
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
        int lang = unpacker.unpackInt();
        int paramCount = unpacker.unpackInt();

        Value[] arguments = prepareArgs.getArgs();
        for (int i = 0; i < paramCount; i++) {
            int mode = unpacker.unpackInt();
            int type = unpacker.unpackInt();
            Value val = arguments[i];

            val.setMode(mode);
            val.setDbType(type);
        }
        int returnType = unpacker.unpackInt();

        boolean transactionControl = unpacker.unpackBool();
        getCurrentContext().setTransactionControl(transactionControl);

        storedProcedure = new StoredProcedure(methodSig, lang, authUser, arguments, returnType);
        return storedProcedure;
    }

    private void returnOutArgs(StoredProcedure sp, CUBRIDPacker packer)
            throws IOException, ExecuteException, TypeMismatchException {
        Value[] args = sp.getArgs();
        for (int i = 0; args != null && i < args.length; i++) {
            if (args[i].getMode() > Value.IN) {
                Value v = sp.makeOutValue(i);
                packer.packValue(v, args[i].getDbType());
            }
        }
    }

    private void sendResult(Value result, StoredProcedure procedure)
            throws IOException, ExecuteException, TypeMismatchException {
        resultBuffer.clear(); /* prepare to put */
        packer.setBuffer(resultBuffer);

        packer.packInt(RequestCode.RESULT);
        packer.align(DataUtilities.MAX_ALIGNMENT);
        packer.packValue(result, procedure.getReturnType());
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
