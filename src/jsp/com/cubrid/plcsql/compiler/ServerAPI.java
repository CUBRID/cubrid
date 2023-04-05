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

package com.cubrid.plcsql.compiler;

import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.data.ColumnInfo;
import com.cubrid.jsp.data.DBType;
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
import java.util.ArrayList;
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
            if (errCode == 0) {
                PlParamInfo dummy = new PlParamInfo(unpacker);

                int paramSize = (int) unpacker.unpackBigint();
                if (paramSize > 0) {
                    params = new PlParamInfo[paramSize];
                    for (int i = 0; i < params.length; i++) {
                        params[i] = new PlParamInfo(unpacker);
                    }
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
            if (errCode == 0) {
                retType = new PlParamInfo(unpacker);

                int paramSize = (int) unpacker.unpackBigint();
                if (paramSize > 0) {
                    params = new PlParamInfo[paramSize];
                    for (int i = 0; i < params.length; i++) {
                        params[i] = new PlParamInfo(unpacker);
                    }
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
            if (errCode == 0) {
                colType = new ColumnInfo(unpacker);
            }
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

    // -----------------------------------------
    // Private
    // -----------------------------------------

    private static List<SqlSemantics> getMockSqlSemantics(List<String> sqlTexts) {
        List<SqlSemantics> ret = new ArrayList<>();

        int seqNo = 0;
        for (String sql : sqlTexts) {
            sql = sql.toUpperCase();

            if (sql.startsWith("SELECT")) {
                // select code, name from athlete where gender = g and nation_code = n;
                // or
                // select code, name into c, m from athlete where gender = g and nation_code = n;

                List<PlParamInfo> hostVars = new ArrayList<>();

                hostVars.add(
                        new PlParamInfo(
                                "G", // name
                                ServerConstants.PARAM_MODE_IN, // mode
                                DBType.DB_CHAR, // type
                                1, // prec
                                (short) 0, // scale
                                ServerConstants.CUBRID_CHARSET_UTF8) // charset
                        );
                hostVars.add(
                        new PlParamInfo(
                                "N", // name
                                ServerConstants.PARAM_MODE_IN, // mode
                                DBType.DB_CHAR, // type
                                3, // prec
                                (short) 0, // scale
                                ServerConstants.CUBRID_CHARSET_UTF8) // charset
                        );

                List<ColumnInfo> selectList = new ArrayList<>();

                ColumnInfo codeInfo = new ColumnInfo();
                codeInfo.colName = "CODE";
                codeInfo.type = DBType.DB_INT;
                selectList.add(codeInfo);

                ColumnInfo nameInfo = new ColumnInfo();
                nameInfo.colName = "NAME";
                nameInfo.type = DBType.DB_STRING;
                nameInfo.prec = 40;
                selectList.add(nameInfo);

                List<String> intoVars;
                if (sql.indexOf("INTO") < 0) {
                    intoVars = null;
                } else {
                    intoVars = new ArrayList<>();
                    intoVars.add("C");
                    intoVars.add("M");
                    if (sql.indexOf("C , M , E") >= 0) {
                        intoVars.add("E"); // to cause error s402
                    }
                }

                ret.add(
                        new SqlSemantics(
                                seqNo++,
                                ServerConstants.CUBRID_STMT_SELECT,
                                "select code, name from athlete where gender = ? and nation_code = ?",
                                hostVars,
                                selectList,
                                intoVars));

            } else if (sql.startsWith("INSERT")) {
                // insert into athlete(name, gender) values (n, g);

                List<PlParamInfo> hostVars = new ArrayList<>();

                hostVars.add(
                        new PlParamInfo(
                                "N", // name
                                ServerConstants.PARAM_MODE_IN, // mode
                                DBType.DB_STRING, // type
                                40, // prec
                                (short) -1, // scale
                                ServerConstants.CUBRID_CHARSET_UTF8) // charset
                        );
                hostVars.add(
                        new PlParamInfo(
                                "G", // name
                                ServerConstants.PARAM_MODE_IN, // mode
                                DBType.DB_CHAR, // type
                                1, // prec
                                (short) -1, // scale
                                ServerConstants.CUBRID_CHARSET_UTF8) // charset
                        );

                ret.add(
                        new SqlSemantics(
                                seqNo++,
                                ServerConstants.CUBRID_STMT_INSERT,
                                "insert into athlete(name, gender) values (?, ?)",
                                hostVars,
                                null,
                                null));
            } else {
                ret.add(new SqlSemantics(seqNo++, 300, "mock error"));
            }
        }

        return ret;
    }

    private static final PlParamInfo[] MOCK_PARAM_TYPES =
            new PlParamInfo[] {
                new PlParamInfo(
                        null, // name
                        ServerConstants.PARAM_MODE_IN, // mode
                        DBType.DB_INT, // type
                        -1, // prec
                        (short) -1, // scale
                        (byte) -1), // charset
                new PlParamInfo(
                        null, // name
                        ServerConstants.PARAM_MODE_OUT, // mode
                        DBType.DB_STRING, // type
                        -1, // prec
                        (short) -1, // scale
                        (byte) -1), // charset
                new PlParamInfo(
                        null, // name
                        ServerConstants.PARAM_MODE_OUT, // mode
                        DBType.DB_FLOAT, // type
                        -1, // prec
                        (short) -1, // scale
                        (byte) -1) // charset
            };
    private static final PlParamInfo[] ERR_PARAM_TYPES =
            new PlParamInfo[] {
                new PlParamInfo(
                        null, // name
                        ServerConstants.PARAM_MODE_IN, // mode
                        DBType.DB_INT, // type
                        -1, // prec
                        (short) -1, // scale
                        (byte) -1), // charset
                new PlParamInfo(
                        null, // name
                        ServerConstants.PARAM_MODE_OUT, // mode
                        DBType.DB_DATETIMELTZ, // type
                        -1, // prec
                        (short) -1, // scale
                        (byte) -1), // charset
                new PlParamInfo(
                        null, // name
                        ServerConstants.PARAM_MODE_OUT, // mode
                        DBType.DB_TIMESTAMPLTZ, // type
                        -1, // prec
                        (short) -1, // scale
                        (byte) -1) // charset
            };
    private static final PlParamInfo mockType =
            new PlParamInfo(
                    null, // name
                    (byte) -1, // mode
                    DBType.DB_STRING, // type
                    -1, // prec
                    (short) -1, // scale
                    (byte) -1); // charset
    private static final PlParamInfo errType =
            new PlParamInfo(
                    null, // name
                    (byte) -1, // mode
                    DBType.DB_DATETIMELTZ, // type
                    -1, // prec
                    (short) -1, // scale
                    (byte) -1); // charset
    private static final ColumnInfo mockColType = new ColumnInfo();
    private static final ColumnInfo errColType = new ColumnInfo();

    static {
        mockColType.type = DBType.DB_STRING;
        errColType.type = DBType.DB_DATETIMELTZ;
    }

    private static List<Question> getMockGlobalSemantics(List<Question> questions) {

        // MOCK

        int seqNo = 0;
        for (Question q : questions) {
            if (q instanceof ProcedureSignature) {
                ProcedureSignature ps = (ProcedureSignature) q;
                if (ps.name.equals("MY_PROC")) {
                    ps.setAnswer(seqNo++, MOCK_PARAM_TYPES);
                } else if (ps.name.equals("ERR_PROC")) {
                    ps.setAnswer(seqNo++, ERR_PARAM_TYPES); // to cause error s412
                } else {
                    ps.setError(seqNo++, 300, "no such procedure " + ps.name);
                }
            } else if (q instanceof FunctionSignature) {
                FunctionSignature fs = (FunctionSignature) q;
                if (fs.name.equals("MY_FUNC")) {
                    fs.setAnswer(seqNo++, MOCK_PARAM_TYPES, mockType);
                } else if (fs.name.equals("ERR_FUNC")) {
                    fs.setAnswer(seqNo++, ERR_PARAM_TYPES, mockType); // to cause error s415
                } else if (fs.name.equals("ERR_FUNC_2")) {
                    fs.setAnswer(seqNo++, MOCK_PARAM_TYPES, errType); // to cause error s418
                } else {
                    fs.setError(seqNo++, 301, "no such function " + fs.name);
                }
            } else if (q instanceof SerialOrNot) {
                SerialOrNot sn = (SerialOrNot) q;
                if (sn.name.equals("MY_SERIAL")) {
                    sn.setAnswer(seqNo++);
                } else {
                    sn.setError(seqNo++, 302, "no such serial " + sn.name);
                }

            } else if (q instanceof ColumnType) {
                ColumnType ct = (ColumnType) q;
                String s = ct.table + "." + ct.column;
                if (s.equals("MY_TABLE.MY_COLUMN")) {
                    ct.setAnswer(seqNo++, mockColType);
                } else if (s.equals("ERR_TABLE.ERR_COLUMN")) {
                    ct.setAnswer(seqNo++, errColType); // to cause error s410
                } else {
                    ct.setError(seqNo++, 303, "no such table column " + s);
                }
            } else {
                assert false : "unreachable";
            }
        }

        return questions;
    }
}
