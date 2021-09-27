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

package com.cubrid.jsp.io;
import com.cubrid.jsp.data.FetchInfo;
import com.cubrid.jsp.data.PrepareInfo;
import com.cubrid.jsp.ExecuteThread;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.data.DBParameterInfo;
import com.cubrid.jsp.data.ExecuteInfo;
import com.cubrid.jsp.data.GetByOIDInfo;
import com.cubrid.jsp.data.GetSchemaInfo;
import com.cubrid.jsp.data.SOID;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.io.SUFunctionCode;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;
import java.sql.Connection;
import java.util.ArrayList;
import java.util.List;

import cubrid.jdbc.jci.UError;

public class SUConnection {

    List<SUStatement> stmts = null;
    ExecuteThread thread = null;
    ByteBuffer outputBuffer = ByteBuffer.allocate(4096);

    public SUConnection (ExecuteThread t) {
        thread = t;
        stmts = new ArrayList<SUStatement> ();
    }

    public CUBRIDUnpacker request (ByteBuffer buffer) throws IOException
    {
        thread.sendCommand(buffer);
        buffer.clear();
        return thread.receiveBuffer();
    }

    // UFunctionCode.GET_DB_PARAMETER
    public DBParameterInfo getDBParameter () throws IOException {
        CUBRIDPacker packer = new CUBRIDPacker (outputBuffer);
        packer.packInt (SUFunctionCode.GET_DB_PARAMETER.getCode());

        CUBRIDUnpacker unpacker = request (outputBuffer);
        DBParameterInfo info = new DBParameterInfo (unpacker);
        return info;
    }

    // UFunctionCode.PREPARE
    public SUStatement prepare (String sql, byte flag) throws IOException {
        return prepare (sql, flag, false);
    }

    public SUStatement prepare (String sql, byte flag, boolean recompile) throws IOException {
        CUBRIDPacker packer = new CUBRIDPacker (outputBuffer);
        packer.packInt (SUFunctionCode.PREPARE.getCode());
        packer.packString (sql);
        packer.packInt (flag);
        
        CUBRIDUnpacker unpacker = request (outputBuffer);
        PrepareInfo info = new PrepareInfo (unpacker);

        SUStatement stmt = null;
        if (recompile) {
            stmt = new SUStatement (this, info, true, sql, flag);
        } else {
            stmt = new SUStatement (this, info, false, sql, flag);
        }

        stmts.add(stmt);
        return stmt;
    }

    // UFunctionCode.GET_BY_OID
    public SUStatement getByOID(SOID oid, String[] attributeName) throws IOException {
        CUBRIDPacker packer = new CUBRIDPacker (outputBuffer);
        packer.packInt (SUFunctionCode.GET_BY_OID.getCode());
        packer.packOID (oid);
        packer.packInt (attributeName.length);
        if (attributeName != null) {
            for (int i = 0; i < attributeName.length; i++)
            {
                packer.packString(attributeName[i]);
            }
        }

        CUBRIDUnpacker unpacker = request (outputBuffer);
        GetByOIDInfo info = new GetByOIDInfo (unpacker);
        SUStatement stmt = new SUStatement(this, info, attributeName);
        return stmt;
    }

    // UFunctionCode.GET_SCHEMA_INFO
    public SUStatement getSchemaInfo(int type, String arg1, String arg2, byte flag) throws IOException {
        CUBRIDPacker packer = new CUBRIDPacker (outputBuffer);
        packer.packInt (SUFunctionCode.GET_SCHEMA_INFO.getCode());
        packer.packInt (type);
        packer.packString (arg1);
        packer.packString (arg2);
        packer.packInt(flag);

        CUBRIDUnpacker unpacker = request (outputBuffer);
        GetSchemaInfo info = new GetSchemaInfo (unpacker);
        SUStatement stmt = new SUStatement(this, info, arg1, arg2, type);
        return stmt;
    }

    // UFunctionCode.EXECUTE
    public ExecuteInfo execute (
        int handlerId, 
        byte executeFlag, 
        boolean isScrollable, 
        int maxField, 
        SUBindParameter bindParameter) throws IOException {
            CUBRIDPacker packer = new CUBRIDPacker (outputBuffer);
            packer.packInt (SUFunctionCode.EXECUTE.getCode());
            packer.packInt (handlerId);
            packer.packInt (executeFlag);
            packer.packInt (maxField < 0 ? 0 : maxField);

            if (isScrollable == false) {
                packer.packInt (1); // isForwardOnly = true
            } else {
                packer.packInt (0); // isForwardOnly = false
            }

            int hasParam = (bindParameter != null) ? 2 : 0;
            packer.packInt (hasParam);
            if (bindParameter != null) {
                bindParameter.pack(packer);
            }

        CUBRIDUnpacker unpacker = request (outputBuffer);
        ExecuteInfo info = new ExecuteInfo (unpacker);
        return info;
    }

    // UFunctionCode.FETCH
    public FetchInfo fetch (
        int currentRowIndex,
        int fetchSize, int fetchFlag) throws IOException, TypeMismatchException {
            CUBRIDPacker packer = new CUBRIDPacker (outputBuffer);
            packer.packInt (SUFunctionCode.FETCH.getCode());
            packer.packInt (currentRowIndex);
            packer.packInt (fetchSize);
            packer.packInt (fetchFlag);

        CUBRIDUnpacker unpacker = request (outputBuffer);
        FetchInfo info = new FetchInfo (unpacker);
        return info;
    }

    // UFunctionCode.NEXT_RESULT
    public ExecuteInfo nextResult (int handlerId) throws IOException {
        CUBRIDPacker packer = new CUBRIDPacker (outputBuffer);
        packer.packInt (SUFunctionCode.NEXT_RESULT.getCode());
        packer.packInt (handlerId);

        CUBRIDUnpacker unpacker = request (outputBuffer);
        ExecuteInfo info = new ExecuteInfo (unpacker);
        return info;
    }

    // UFunctionCode.NEW_LOB
    // UFunctionCode.WRITE_LOB
    // UFunctionCode.READ_LOB
    // UFunctionCode.RELATED_TO_COLLECTION
}
