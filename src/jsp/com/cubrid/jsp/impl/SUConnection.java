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

package com.cubrid.jsp.impl;

import com.cubrid.jsp.ExecuteThread;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.data.DBParameterInfo;
import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.data.ErrorInfo;
import com.cubrid.jsp.data.ExecuteInfo;
import com.cubrid.jsp.data.FetchInfo;
import com.cubrid.jsp.data.GetByOIDInfo;
import com.cubrid.jsp.data.GetGeneratedKeysInfo;
import com.cubrid.jsp.data.GetSchemaInfo;
import com.cubrid.jsp.data.MakeOutResultSetInfo;
import com.cubrid.jsp.data.PrepareInfo;
import com.cubrid.jsp.data.SOID;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConstants;
import com.cubrid.jsp.jdbc.CUBRIDServerSideJDBCErrorCode;
import com.cubrid.jsp.jdbc.CUBRIDServerSideJDBCErrorManager;
import cubrid.sql.CUBRIDOID;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.sql.SQLException;

public class SUConnection {

    ExecuteThread thread = null;
    ByteBuffer outputBuffer = ByteBuffer.allocate(4096);

    public SUConnection(ExecuteThread t) {
        thread = t;
    }

    public CUBRIDUnpacker request(ByteBuffer buffer) throws IOException, SQLException {
        thread.sendCommand(buffer);
        buffer.clear();
        CUBRIDUnpacker unpacker = thread.receiveBuffer();

        int responseCode = unpacker.unpackInt();
        if (responseCode != 0) {
            ErrorInfo errorInfo = new ErrorInfo(unpacker);
            String errorMsg = errorInfo.errorString;
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_DBMS, errorMsg, null);
        }

        return unpacker;
    }

    // UFunctionCode.GET_DB_PARAMETER
    public DBParameterInfo getDBParameter() throws IOException, SQLException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.GET_DB_PARAMETER.getCode());

        CUBRIDUnpacker unpacker = request(outputBuffer);
        DBParameterInfo info = new DBParameterInfo(unpacker);
        return info;
    }

    // UFunctionCode.PREPARE
    public SUStatement prepare(String sql, byte flag) throws IOException, SQLException {
        return prepare(sql, flag, false);
    }

    public SUStatement prepare(String sql, byte flag, boolean recompile)
            throws IOException, SQLException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.PREPARE.getCode());
        packer.packString(sql);
        packer.packInt(flag);

        CUBRIDUnpacker unpacker = request(outputBuffer);
        PrepareInfo info = new PrepareInfo(unpacker);

        SUStatement stmt = null;
        if (recompile) {
            stmt = new SUStatement(this, info, true, sql, flag);
        } else {
            stmt = new SUStatement(this, info, false, sql, flag);
        }

        return stmt;
    }

    // UFunctionCode.GET_SCHEMA_INFO
    public SUStatement getSchemaInfo(int type, String arg1, String arg2, byte flag)
            throws IOException, SQLException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.GET_SCHEMA_INFO.getCode());
        packer.packInt(type);
        packer.packString(arg1);
        packer.packString(arg2);
        packer.packInt(flag);

        CUBRIDUnpacker unpacker = request(outputBuffer);
        GetSchemaInfo info = new GetSchemaInfo(unpacker);
        SUStatement stmt = new SUStatement(this, info, arg1, arg2, type);
        return stmt;
    }

    // UFunctionCode.EXECUTE
    public ExecuteInfo execute(
            int handlerId,
            byte executeFlag,
            boolean isScrollable,
            int maxField,
            SUBindParameter bindParameter)
            throws IOException, SQLException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.EXECUTE.getCode());
        packer.packInt(handlerId);
        packer.packInt(executeFlag);
        packer.packInt(maxField < 0 ? 0 : maxField);

        if (isScrollable == false) {
            packer.packInt(1); // isForwardOnly = true
        } else {
            packer.packInt(0); // isForwardOnly = false
        }

        int hasParam = (bindParameter != null) ? 2 : 0;
        packer.packInt(hasParam);
        if (bindParameter != null) {
            bindParameter.pack(packer);
        }

        CUBRIDUnpacker unpacker = request(outputBuffer);
        ExecuteInfo info = new ExecuteInfo(unpacker);
        return info;
    }

    // UFunctionCode.FETCH
    public FetchInfo fetch(long queryId, int currentRowIndex, int fetchSize, int fetchFlag)
            throws IOException, TypeMismatchException, SQLException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.FETCH.getCode());
        packer.packBigInt(queryId);
        packer.packInt(currentRowIndex);
        packer.packInt(fetchSize);
        packer.packInt(fetchFlag);

        CUBRIDUnpacker unpacker = request(outputBuffer);
        FetchInfo info = new FetchInfo(unpacker);
        return info;
    }

    // UFunctionCode.MAKE_OUT_RS
    public MakeOutResultSetInfo makeOutResult(long queryId) throws IOException, SQLException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.MAKE_OUT_RS.getCode());
        packer.packBigInt(queryId);

        CUBRIDUnpacker unpacker = request(outputBuffer);
        MakeOutResultSetInfo info = new MakeOutResultSetInfo(unpacker);
        return info;
    }

    // UFunctionCode.NEXT_RESULT
    public ExecuteInfo nextResult(int handlerId) throws IOException, SQLException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.NEXT_RESULT.getCode());
        packer.packInt(handlerId);

        CUBRIDUnpacker unpacker = request(outputBuffer);
        ExecuteInfo info = new ExecuteInfo(unpacker);
        return info;
    }

    // UFunctionCode.GET_BY_OID
    public SUStatement getByOID(CUBRIDOID oid, String[] attributeName)
            throws IOException, SQLException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.GET_BY_OID.getCode());
        packer.packOID(new SOID(oid.getOID()));

        if (attributeName != null) {
            packer.packInt(attributeName.length);
            for (int i = 0; i < attributeName.length; i++) {
                if (attributeName[i] != null) {
                    packer.packString(attributeName[i]);
                } else {
                    packer.packString("");
                }
            }
        } else {
            packer.packInt(0);
        }

        CUBRIDUnpacker unpacker = request(outputBuffer);
        GetByOIDInfo info = new GetByOIDInfo(unpacker);
        SUStatement stmt = new SUStatement(this, info, oid, attributeName);
        return stmt;
    }

    // UFunctionCode.GET_GENERATED_KEYS
    public SUStatement getGeneratedKeys(int handlerId)
            throws IOException, SQLException, TypeMismatchException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.GET_GENERATED_KEYS.getCode());
        packer.packInt(handlerId);

        CUBRIDUnpacker unpacker = request(outputBuffer);
        GetGeneratedKeysInfo info = new GetGeneratedKeysInfo(unpacker);
        SUStatement stmt = new SUStatement(this, info);
        return stmt;
    }

    // UFunctionCode.PUT_BY_OID
    public void putByOID(CUBRIDOID oid, String[] attributeName, Object values[])
            throws IOException, SQLException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.PUT_BY_OID.getCode());
        packer.packOID(new SOID(oid.getOID()));

        if (attributeName != null) {
            packer.packInt(attributeName.length);
            for (int i = 0; i < attributeName.length; i++) {
                if (attributeName[i] != null) {
                    packer.packString(attributeName[i]);
                } else {
                    packer.packString("");
                }

                int type = DBType.getObjectDBtype(values[i]);
                packer.packValue(values[i], type, "UTF-8");
            }
        } else {
            packer.packInt(0);
        }

        CUBRIDUnpacker unpacker = request(outputBuffer);
        int result = unpacker.unpackInt();
    }

    // UFunctionCode.RELATED_TO_OID
    public Object oidCmd(CUBRIDOID oid, int command) throws IOException, SQLException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.RELATED_TO_OID.getCode());
        packer.packInt(command);
        packer.packOID(new SOID(oid.getOID()));

        CUBRIDUnpacker unpacker = request(outputBuffer);
        int result = unpacker.unpackInt();
        if (command == CUBRIDServerSideConstants.IS_INSTANCE) {
            if (result == 1) {
                return oid;
            }
        } else if (command == CUBRIDServerSideConstants.GET_CLASS_NAME_BY_OID) {
            return unpacker.unpackCString();
        }

        return null;
    }

    // UFunctionCode.RELATED_TO_COLLECTION
    protected CUBRIDUnpacker collectionCmd(
            int cmd, CUBRIDOID oid, String attributeName, Object value, int index)
            throws IOException, SQLException {
        CUBRIDPacker packer = new CUBRIDPacker(outputBuffer);
        packer.packInt(SUFunctionCode.RELATED_TO_COLLECTION.getCode());
        packer.packInt(cmd);
        packer.packOID(new SOID(oid.getOID()));
        packer.packInt(index);

        if (attributeName != null) {
            packer.packString(attributeName);
        } else {
            packer.packString("");
        }

        if (value != null) {
            packer.packInt(1); // has value
            int type = DBType.getObjectDBtype(value);
            packer.packValue(value, type, "UTF-8");
        } else {
            packer.packInt(0); // has value
        }

        CUBRIDUnpacker unpacker = request(outputBuffer);
        return unpacker;
    }

    public void addElementToSet(CUBRIDOID oid, String attributeName, Object value)
            throws IOException, SQLException {
        collectionCmd(CUBRIDServerSideConstants.ADD_ELEMENT_TO_SET, oid, attributeName, value, -1);
    }

    public void dropElementInSet(CUBRIDOID oid, String attributeName, Object value)
            throws IOException, SQLException {
        collectionCmd(CUBRIDServerSideConstants.DROP_ELEMENT_IN_SET, oid, attributeName, value, -1);
    }

    public void putElementInSequence(CUBRIDOID oid, String attributeName, int index, Object value)
            throws IOException, SQLException {
        collectionCmd(
                CUBRIDServerSideConstants.PUT_ELEMENT_ON_SEQUENCE,
                oid,
                attributeName,
                value,
                index);
    }

    public void insertElementIntoSequence(
            CUBRIDOID oid, String attributeName, int index, Object value)
            throws IOException, SQLException {
        collectionCmd(
                CUBRIDServerSideConstants.INSERT_ELEMENT_INTO_SEQUENCE,
                oid,
                attributeName,
                value,
                index);
    }

    public void dropElementInSequence(CUBRIDOID oid, String attributeName, int index)
            throws IOException, SQLException {
        collectionCmd(
                CUBRIDServerSideConstants.DROP_ELEMENT_IN_SEQUENCE,
                oid,
                attributeName,
                null,
                index);
    }

    public int getSizeOfCollection(CUBRIDOID oid, String attributeName)
            throws IOException, SQLException {
        int size = 0;
        CUBRIDUnpacker unpacker =
                collectionCmd(
                        CUBRIDServerSideConstants.GET_SIZE_OF_COLLECTION,
                        oid,
                        attributeName,
                        null,
                        -1);
        size = unpacker.unpackInt();
        return size;
    }

    // UFunctionCode.NEW_LOB
    // UFunctionCode.WRITE_LOB
    // UFunctionCode.READ_LOB
}
