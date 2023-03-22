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

<<<<<<< HEAD
import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.protocol.Header;
import com.cubrid.jsp.protocol.RequestCode;
import com.cubrid.jsp.protocol.SqlSemanticsRequest;
import com.cubrid.jsp.protocol.SqlSemanticsResponse;
import java.io.IOException;
import java.nio.ByteBuffer;
=======
>>>>>>> upstream/feature/plcsql
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;

public class ServerAPI {

    public static List<SqlSemantics> getSqlSemantics(List<String> sqlTexts) {
<<<<<<< HEAD
        if (sqlTexts == null) {
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

            SqlSemanticsResponse response = new SqlSemanticsResponse(unpacker);
            return response.semantics;
        } catch (IOException e) {
            // TODO
            return null;
        }
=======
        return getMockSqlSemantics(sqlTexts);
>>>>>>> upstream/feature/plcsql
    }

    public static List<Question> getGlobalSemantics(List<Question> questions) {
        return getMockGlobalSemantics(questions);
    }

    // --------------------------------------------------------

    public static class Question {
        public int errCode;
        public String errMsg;

        public void setError(int errCode, String errMsg) {
            this.errCode = errCode;
            this.errMsg = errMsg;
        }
    }

    public static class ProcedureSignature extends Question {

        public ProcedureSignature(String name) {
            this.name = name;
        }

        // input
        public String name; // procedure name

        // output
        public int[] outPositions; // 1 for out/in-out parameters, otherwise 0
        public String[] paramTypes; // SQL types of parameters

        public void setAnswer(int[] outPositions, String[] paramTypes) {
            assert outPositions.length == paramTypes.length;
            this.outPositions = outPositions;
            this.paramTypes = paramTypes;
        }
    }

    public static class FunctionSignature extends Question {

        public FunctionSignature(String name) {
            this.name = name;
        }

        // input
        public String name; // function name

        // output
        public int[] outPositions; // 1 for out/in-out parameters, otherwise 0
        public String[] paramTypes; // SQL types of parameters
        public String retType; // SQL type

        public void setAnswer(int[] outPositions, String[] paramTypes, String retType) {
            assert outPositions.length == paramTypes.length;
            this.outPositions = outPositions;
            this.paramTypes = paramTypes;
            this.retType = retType;
        }
    }

    public static class SerialOrNot extends Question {

        public SerialOrNot(String name) {
            this.name = name;
        }

        // intput
        public String name;

        // no separate output: the existence or absence of an error is the output
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
        public String type; // SQL type if the column exists, otherwise null

        public void setAnswer(String type) {
            this.type = type;
        }
    }

    // -----------------------------------------
    // Private
    // -----------------------------------------

    private static List<SqlSemantics> getMockSqlSemantics(List<String> sqlTexts) {
<<<<<<< HEAD
=======

        // MOCK

>>>>>>> upstream/feature/plcsql
        List<SqlSemantics> ret = new ArrayList<>();

        for (String sql : sqlTexts) {
            sql = sql.toUpperCase();

            if (sql.startsWith("SELECT")) {
<<<<<<< HEAD
=======
                // select code, name from athlete where gender = g and nation_code = n;
                // or
                // select code, name into c, m from athlete where gender = g and nation_code = n;

>>>>>>> upstream/feature/plcsql
                LinkedHashMap<String, String> hostVars = new LinkedHashMap<>();
                hostVars.put("G", "CHARACTER(1)");
                hostVars.put("N", "CHARACTER(3)");

                LinkedHashMap<String, String> selectList = new LinkedHashMap<>();
                selectList.put("CODE", "INTEGER");
                selectList.put("NAME", "CHARACTER VARYING(40)");

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
                                ServerConstants.CUBRID_STMT_SELECT,
                                "select code, name from athlete where gender = ? and nation_code = ?",
                                hostVars,
                                selectList,
                                intoVars));

            } else if (sql.startsWith("INSERT")) {
                // insert into athlete(name, gender) values (n, g);

                LinkedHashMap<String, String> hostVars = new LinkedHashMap<>();
                hostVars.put("N", "CHARACTER VARYING(40)");
                hostVars.put("G", "CHARACTER(1)");

                ret.add(
                        new SqlSemantics(
                                ServerConstants.CUBRID_STMT_INSERT,
                                "insert into athlete(name, gender) values (?, ?)",
                                hostVars,
                                null,
                                null));
            } else {
                ret.add(new SqlSemantics(300, "mock error"));
            }
        }

        return ret;
    }

    private static final int[] MOCK_OUT_POS = new int[] {0, 1, 1};
    private static final String[] MOCK_PARAM_TYPES = new String[] {"INTEGER", "VARCHAR", "FLOAT"};
    private static final String[] ERR_PARAM_TYPES = new String[] {"INTEGER", "BLOB", "CLOB"};

    private static List<Question> getMockGlobalSemantics(List<Question> questions) {

        // MOCK

        for (Question q : questions) {
            if (q instanceof ProcedureSignature) {
                ProcedureSignature ps = (ProcedureSignature) q;
                if (ps.name.equals("MY_PROC")) {
                    ps.setAnswer(MOCK_OUT_POS, MOCK_PARAM_TYPES);
                } else if (ps.name.equals("ERR_PROC")) {
                    ps.setAnswer(MOCK_OUT_POS, ERR_PARAM_TYPES); // to cause error s412
                } else {
                    ps.setError(300, "no such procedure " + ps.name);
                }
            } else if (q instanceof FunctionSignature) {
                FunctionSignature fs = (FunctionSignature) q;
                if (fs.name.equals("MY_FUNC")) {
                    fs.setAnswer(MOCK_OUT_POS, MOCK_PARAM_TYPES, "VARCHAR");
                } else if (fs.name.equals("ERR_FUNC")) {
                    fs.setAnswer(MOCK_OUT_POS, ERR_PARAM_TYPES, "VARCHAR"); // to cause error s415
                } else if (fs.name.equals("ERR_FUNC_2")) {
                    fs.setAnswer(MOCK_OUT_POS, MOCK_PARAM_TYPES, "BLOB"); // to cause error s418
                } else {
                    fs.setError(301, "no such function " + fs.name);
                }
            } else if (q instanceof SerialOrNot) {
                SerialOrNot sn = (SerialOrNot) q;
                if (sn.name.equals("MY_SERIAL")) {
                    // OK
                } else {
                    sn.setError(302, "no such serial " + sn.name);
                }

            } else if (q instanceof ColumnType) {
                ColumnType ct = (ColumnType) q;
                String s = ct.table + "." + ct.column;
                if (s.equals("MY_TABLE.MY_COLUMN")) {
                    ct.setAnswer("VARCHAR");
                } else if (s.equals("ERR_TABLE.ERR_COLUMN")) {
                    ct.setAnswer("BLOB"); // to cause error s410
                } else {
                    ct.setError(303, "no such table column " + s);
                }
            } else {
                assert false : "unreachable";
            }
        }

        return questions;
    }
}
