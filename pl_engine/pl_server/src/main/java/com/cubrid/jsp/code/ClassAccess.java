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
package com.cubrid.jsp.code;

import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.protocol.Header;
import com.cubrid.jsp.protocol.RequestCode;
import com.cubrid.jsp.value.Value;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Base64;

public class ClassAccess {

    // get object code of the SP being invoked
    public static CompiledCodeSet getObjectCodeOfCurrentInvoke() {

        try {
            byte[] jarCode = getObjectCodeBytes();
            if (jarCode != null) {
                return CompiledCodeSet.loadFromJar(jarCode);
                // mainClassName, and compileId will be set later
            } else {
                return null;
            }
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    // duringCompile distinguishes the request framing: while compiling, the request is handled by
    // the compile handler; at run time it is handled by the executor's callback loop.
    public static CompiledCodeSet getObjectCodeOf(String mainClassName, boolean duringCompile) {

        // get the object code of given class name from the ocode column of
        // _db_stored_procedure_code or
        // _db_package_code, or return null if no item is found for that class name.

        try {
            String[] compileIdRef = new String[1];
            byte[] jarCode =
                    getObjectCodeBytesWithNameAndId(
                            mainClassName, null, compileIdRef, duringCompile);
            if (jarCode == null) {
                return null;
            } else {
                assert jarCode.length > 0;

                CompiledCodeSet code = CompiledCodeSet.loadFromJar(jarCode);
                code.setMainClassName(mainClassName);
                code.setCompileId(compileIdRef[0]);
                return code;
            }
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    public static CompiledCodeSet getObjectCodeNewerThan(CompiledCodeSet codeSet) {

        // get the object code of given class name from the ocode column of
        // _db_stored_procedure_code or
        // _db_package_code.
        // if no record exist with the codeSet's main class name, then return null.
        // if the current compileId of the code in the table is the same as that of codeSet, just
        // return the given codeSet.
        // otherwise, return a new CompiledCodeSet.

        try {
            String[] compileIdRef = new String[1];
            byte[] jarCode =
                    getObjectCodeBytesWithNameAndId(
                            codeSet.mainClassName, codeSet.compileId, compileIdRef, false);
            if (jarCode == null) {
                return null;
            } else if (jarCode.length == 0) {
                return codeSet;
            } else {
                CompiledCodeSet code = CompiledCodeSet.loadFromJar(jarCode);
                code.setMainClassName(codeSet.mainClassName);
                code.setCompileId(compileIdRef[0]);
                return code;
            }
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    // ======================
    // Private
    // ======================

    private static byte[] EMPTY_BYTES = new byte[0];

    // status codes shared with the server (see SP_CODE_FETCH_STATUS in sp_code.hpp)
    private static final int STATUS_NOT_FOUND = 0;
    private static final int STATUS_UNCHANGED = 1;
    private static final int STATUS_CHANGED = 2;

    private static byte[] getObjectCodeBytesWithNameAndId(
            String mainClassName, String compileId, String[] compileIdRef, boolean duringCompile)
            throws IOException {
        // Ask the server (via a dedicated protocol) for the ocode of the SP/package whose generated
        // class name is mainClassName. The server reads the catalog with authorization disabled, so
        // this works even when the referenced unit is owned by another user.
        // The result of this method can be one of the following three:
        //   - null        : no such SP/package (e.g. dropped)
        //   - EMPTY_BYTES : compileId is given and equals the stored compile_id (i.e. caller is
        // up-to-date)
        //   - otherwise   : compileIdRef[0] is set to the stored compile_id and the ocode bytes are
        //                    returned (the ocode column is a base64-encoded jar)
        CUBRIDPacker packer = new CUBRIDPacker(ByteBuffer.allocate(1024));
        if (duringCompile) {
            // the compile handler dispatches on the explicit outer request code, so the payload
            // carries only the arguments
            packer.packString(mainClassName);
            packer.packString(compileId == null ? "" : compileId);
            Context.getCurrentExecuteThread()
                    .sendCommand(RequestCode.REQUEST_CODE_BY_NAME, packer.getBuffer());
        } else {
            // the executor's callback loop reads the request code from the payload
            packer.packInt(RequestCode.REQUEST_CODE_BY_NAME);
            packer.packString(mainClassName);
            packer.packString(compileId == null ? "" : compileId);
            Context.getCurrentExecuteThread().sendCommand(packer.getBuffer());
        }

        ByteBuffer responseBuffer = Context.getCurrentExecuteThread().receiveBuffer();
        CUBRIDUnpacker unpacker = new CUBRIDUnpacker(responseBuffer);

        Header header = new Header(unpacker);
        ByteBuffer payload = unpacker.unpackBuffer();
        unpacker.setBuffer(payload);

        int error = unpacker.unpackInt();
        if (error != 0) {
            return null;
        }

        int status = unpacker.unpackInt();
        if (status == STATUS_NOT_FOUND) {
            return null;
        } else if (status == STATUS_UNCHANGED) {
            return EMPTY_BYTES;
        } else {
            String newCompileId = unpacker.unpackCString();
            String base64Str = unpacker.unpackCString();
            compileIdRef[0] = newCompileId;
            return Base64.getDecoder().decode(base64Str);
        }
    }

    private static byte[] getObjectCodeBytes() throws IOException, TypeMismatchException {
        byte[] jar = null;

        sendGetCodeAttr("ocode");

        Value val = receiveCodeAttrValue();
        if (val != null) {
            String base64Str = val.toString();
            jar = Base64.getDecoder().decode(base64Str);
        }

        return jar;
    }

    private static void sendGetCodeAttr(String attr_name) throws IOException {
        CUBRIDPacker packer = new CUBRIDPacker(ByteBuffer.allocate(1024));
        packer.packInt(RequestCode.REQUEST_CODE_ATTR);
        packer.packString(attr_name);
        Context.getCurrentExecuteThread().sendCommand(packer.getBuffer());
    }

    private static Value receiveCodeAttrValue() throws IOException, TypeMismatchException {
        ByteBuffer responseBuffer = Context.getCurrentExecuteThread().receiveBuffer();
        CUBRIDUnpacker unpacker = new CUBRIDUnpacker(responseBuffer);

        Header header = new Header(unpacker);
        ByteBuffer payload = unpacker.unpackBuffer();

        unpacker.setBuffer(payload);

        int error = unpacker.unpackInt();
        if (error == 0) {
            int param_type = unpacker.unpackInt();
            Value val = unpacker.unpackValue(param_type);
            return val;
        }

        return null;
    }
}
