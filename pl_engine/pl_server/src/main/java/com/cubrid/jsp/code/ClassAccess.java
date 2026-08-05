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

import com.cubrid.jsp.Server;
import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.protocol.Header;
import com.cubrid.jsp.protocol.RequestCode;
import com.cubrid.jsp.value.Value;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.sql.Connection;
import java.util.Base64;

public class ClassAccess {

    // get object code of the invoked SP
    public static CompiledCodeSet getObjectCode(Connection conn) {

        CompiledCodeSet code = null;

        try {
            byte[] jarCode = getObjectCodeBytes(conn);
            if (jarCode != null) {
                code = CompiledCodeSet.loadFromJar(jarCode);
            }
        } catch (Exception e) {
            Server.log(e);
        }

        return code;
    }

    // ======================
    // Private
    // ======================

    private static byte[] getObjectCodeBytes(Connection conn)
            throws IOException, TypeMismatchException {
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
