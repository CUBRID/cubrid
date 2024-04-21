/*
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

package com.cubrid.plcsql.compiler.serverapi;

import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.data.ColumnInfo;
import com.cubrid.jsp.protocol.GlobalSemanticsRequest;
import com.cubrid.jsp.protocol.GlobalSemanticsResponse;
import com.cubrid.jsp.protocol.Header;
import com.cubrid.jsp.protocol.PackableObject;
import com.cubrid.jsp.protocol.RequestCode;
import com.cubrid.jsp.protocol.SqlSemanticsRequest;
import com.cubrid.jsp.protocol.SqlSemanticsResponse;
import com.cubrid.jsp.protocol.UnPackableObject;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.List;

public class ServerAPI {

    public static List<SqlSemantics> getSqlSemantics(List<String> sqlTexts) {
        if (sqlTexts == null || sqlTexts.size() == 0) {
            return null;
        }

        try {
            CUBRIDPacker packer = new CUBRIDPacker(ByteBuffer.allocate(1024));
            SqlSemanticsRequest request = new SqlSemanticsRequest(sqlTexts);
            packer.packPackableObject(request);
            Context.getCurrentExecuteThread()
                    .sendCommand(RequestCode.REQUEST_SQL_SEMANTICS, packer.getBuffer());

            ByteBuffer responseBuffer = Context.getCurrentExecuteThread().receiveBuffer();
            CUBRIDUnpacker unpacker = new CUBRIDUnpacker(responseBuffer);

            Header header = new Header(unpacker);
            ByteBuffer payload = unpacker.unpackBuffer();
            unpacker.setBuffer(payload);

            int status = unpacker.unpackInt();
            SqlSemanticsResponse response = new SqlSemanticsResponse(unpacker);
            return response.semantics;
        } catch (IOException e) {
            // TODO: error handling
            return null;
        }
    }

    public static List<Question> getGlobalSemantics(List<Question> questions) {
        if (questions == null || questions.size() == 0) {
            return null;
        }

        try {
            CUBRIDPacker packer = new CUBRIDPacker(ByteBuffer.allocate(1024));
            GlobalSemanticsRequest request = new GlobalSemanticsRequest(questions);
            packer.packPackableObject(request);
            Context.getCurrentExecuteThread()
                    .sendCommand(RequestCode.REQUEST_GLOBAL_SEMANTICS, packer.getBuffer());

            ByteBuffer responseBuffer = Context.getCurrentExecuteThread().receiveBuffer();
            CUBRIDUnpacker unpacker = new CUBRIDUnpacker(responseBuffer);

            Header header = new Header(unpacker);
            ByteBuffer payload = unpacker.unpackBuffer();
            unpacker.setBuffer(payload);

            int status = unpacker.unpackInt();
            GlobalSemanticsResponse response = new GlobalSemanticsResponse(questions, unpacker);
            return response.getResponse();
        } catch (IOException e) {
            // TODO: error handling
            return null;
        }
    }

    // --------------------------------------------------------

    public static final int QUESTION_PROCEDURE = 1;
    public static final int QUESTION_FUNCTION = 2;
    public static final int QUESTION_SERIAL = 3;
    public static final int QUESTION_COLUMN = 4;

    public abstract static class Question implements PackableObject, UnPackableObject {
        public int seqNo = -1;
        public int errCode;
        public String errMsg;

        public void setError(int seqNo, int errCode, String errMsg) {
            this.seqNo = seqNo;
            this.errCode = errCode;
            this.errMsg = errMsg;
        }

        @Override
        public void pack(CUBRIDPacker packer) {
            packer.packInt(getType());
        }

        @Override
        public void unpack(CUBRIDUnpacker unpacker) {
            seqNo = unpacker.unpackInt();
            errCode = unpacker.unpackInt();
            errMsg = unpacker.unpackCString();
        }

        public int getType() {
            return -1;
        }
    }

    public static class ProcedureSignature extends Question {

        public ProcedureSignature(String name) {
            this.name = name;
        }

        // input
        public String name; // procedure name

        // output
        public PlParamInfo[] params;

        public void setAnswer(int seqNo, PlParamInfo[] params) {
            this.seqNo = seqNo;
            this.params = params;
        }

        @Override
        public void pack(CUBRIDPacker packer) {
            super.pack(packer);
            packer.packString(name);
        }

        @Override
        public void unpack(CUBRIDUnpacker unpacker) {
            super.unpack(unpacker);
            if (errCode < 0) {
                return;
            }

            PlParamInfo dummy = new PlParamInfo(unpacker);

            int paramSize = (int) unpacker.unpackBigint();
            if (paramSize > 0) {
                params = new PlParamInfo[paramSize];
                for (int i = 0; i < params.length; i++) {
                    params[i] = new PlParamInfo(unpacker);
                }
            }
        }

        @Override
        public int getType() {
            return QUESTION_PROCEDURE;
        }
    }

    public static class FunctionSignature extends Question {

        public FunctionSignature(String name) {
            this.name = name;
        }

        // input
        public String name; // function name

        // output
        public PlParamInfo[] params; // 1 for out/in-out parameters, otherwise 0
        public PlParamInfo retType; // SQL type

        public void setAnswer(int seqNo, PlParamInfo[] params, PlParamInfo retType) {
            this.seqNo = seqNo;
            this.params = params;
            this.retType = retType;
        }

        @Override
        public void unpack(CUBRIDUnpacker unpacker) {
            super.unpack(unpacker);
            if (errCode < 0) {
                return;
            }

            retType = new PlParamInfo(unpacker);

            int paramSize = (int) unpacker.unpackBigint();
            if (paramSize > 0) {
                params = new PlParamInfo[paramSize];
                for (int i = 0; i < params.length; i++) {
                    params[i] = new PlParamInfo(unpacker);
                }
            }
        }

        @Override
        public void pack(CUBRIDPacker packer) {
            super.pack(packer);
            packer.packString(name);
        }

        @Override
        public int getType() {
            return QUESTION_FUNCTION;
        }
    }

    public static class SerialOrNot extends Question {

        public SerialOrNot(String name) {
            this.name = name;
        }

        // intput
        public String name;

        // no separate output: the existence or absence of an error is the output
        public void setAnswer(int seqNo) {
            this.seqNo = seqNo;
        }

        @Override
        public void pack(CUBRIDPacker packer) {
            super.pack(packer);
            packer.packString(name);
        }

        @Override
        public int getType() {
            return QUESTION_SERIAL;
        }
    }

    public static class ColumnType extends Question {

        public ColumnType(String table, String column) {
            this.table = table;
            this.column = column;
        }

        // input
        public String table;
        public String column;

        // output
        public ColumnInfo colType; // SQL type if the column exists, otherwise null

        public void setAnswer(int seqNo, ColumnInfo colType) {
            this.seqNo = seqNo;
            this.colType = colType;
        }

        @Override
        public void unpack(CUBRIDUnpacker unpacker) {
            super.unpack(unpacker);
            if (errCode < 0) {
                return;
            }

            colType = new ColumnInfo(unpacker);
        }

        @Override
        public void pack(CUBRIDPacker packer) {
            super.pack(packer);
            String name = table + "." + column;
            packer.packString(name);
        }

        @Override
        public int getType() {
            return QUESTION_COLUMN;
        }
    }
}
