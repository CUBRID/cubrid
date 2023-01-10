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

package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtExecImme implements Stmt {

    public final boolean isDynamic;
    public final int level;
    public final Expr dynSql;
    public final NodeList<ExprId> intoVarList;
    public final NodeList<? extends Expr> usedExprList;

    public StmtExecImme(
            boolean isDynamic,
            int level,
            Expr dynSql,
            NodeList<ExprId> intoVarList,
            NodeList<? extends Expr> usedExprList) {
        this.isDynamic = isDynamic;
        this.level = level;
        this.dynSql = dynSql;
        this.intoVarList = intoVarList;
        this.usedExprList = usedExprList;
    }

    @Override
    public String toJavaCode() {
        String setUsedValuesStr = Common.getSetUsedValuesStr(usedExprList);

        if (intoVarList == null) {
            // DML statement TODO: check it is not a Select statement
            return tmplDml.replace("%'KIND'%", isDynamic ? "dynamic" : "static")
                    .replace("%'SQL'%", dynSql.toJavaCode())
                    .replace("  %'SET-USED-VALUES'%", Misc.indentLines(setUsedValuesStr, 1))
                    .replace("%'LEVEL'%", "" + level);
        } else {
            // Select statement TODO: check it.
            String setResultsStr = getSetResultsStr(intoVarList);
            String setNullsStr = getSetNullsStr(intoVarList);
            return tmplSelect
                    .replace("%'KIND'%", isDynamic ? "dynamic" : "static")
                    .replace("%'SQL'%", dynSql.toJavaCode())
                    .replace("  %'SET-USED-VALUES'%", Misc.indentLines(setUsedValuesStr, 1))
                    .replace("      %'SET-RESULTS'%", Misc.indentLines(setResultsStr, 3))
                    .replace("    %'SET-NULLS'%", Misc.indentLines(setNullsStr, 2))
                    .replace("%'LEVEL'%", "" + level);
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplDml =
            Misc.combineLines(
                    "{ // %'KIND'% SQL statement",
                    "  String dynSql_%'LEVEL'% = %'SQL'%;",
                    "  PreparedStatement stmt_%'LEVEL'% = conn.prepareStatement(dynSql_%'LEVEL'%);",
                    "  %'SET-USED-VALUES'%",
                    // "  sql_rowcount[0] = (Long) stmt_%'LEVEL'%.executeUpdate();",
                    "  sql_rowcount[0] = stmt_%'LEVEL'%.executeUpdate();",
                    "  stmt_%'LEVEL'%.close();",
                    "}");

    private static final String tmplSelect =
            Misc.combineLines(
                    "{ // %'KIND'% Select statement",
                    "  String dynSql_%'LEVEL'% = %'SQL'%;",
                    "  PreparedStatement stmt_%'LEVEL'% = conn.prepareStatement(dynSql_%'LEVEL'%);",
                    "  %'SET-USED-VALUES'%",
                    "  ResultSet r%'LEVEL'% = stmt_%'LEVEL'%.executeQuery();",
                    "  int i%'LEVEL'% = 0;",
                    "  while (r%'LEVEL'%.next()) {",
                    "    i%'LEVEL'%++;",
                    "    if (i%'LEVEL'% > 1) {",
                    "      break;",
                    "    } else {",
                    "      %'SET-RESULTS'%",
                    "    }",
                    "  }",
                    "  if (i%'LEVEL'% == 0) {",
                    // "    sql_rowcount[0] = 0L;",
                    "    sql_rowcount[0] = 0;",
                    "    %'SET-NULLS'%",
                    "  } else if (i%'LEVEL'% == 1) {",
                    // "    sql_rowcount[0] = 1L;",
                    "    sql_rowcount[0] = 1;",
                    "  } else {",
                    // "    sql_rowcount[0] = 1L; // Surprise? Refer to the Spec.",
                    "    sql_rowcount[0] = 1; // Surprise? Refer to the Spec.",
                    "    throw new RuntimeException(\"too many rows\");",
                    "  }",
                    "  stmt_%'LEVEL'%.close();",
                    "}");

    private static String getSetResultsStr(NodeList<ExprId> idList) {

        StringBuffer sbuf = new StringBuffer();
        int size = idList.nodes.size();
        for (int i = 0; i < size; i++) {
            if (i > 0) {
                sbuf.append("\n");
            }

            ExprId id = idList.nodes.get(i);
            assert id.decl instanceof DeclVar || id.decl instanceof DeclParamOut
                    : "only variables or out-parameters can be used in into-clauses";

            String ty = ((DeclVarLike) id.decl).typeSpec().name;

            sbuf.append(
                    String.format(
                            "%s = (%s) r%%'LEVEL'%%.getObject(%d);", id.toJavaCode(), ty, i + 1));
        }

        return sbuf.toString();
    }

    private static String getSetNullsStr(NodeList<ExprId> idList) {

        StringBuffer sbuf = new StringBuffer();
        int size = idList.nodes.size();
        for (int i = 0; i < size; i++) {
            if (i > 0) {
                sbuf.append("\n");
            }

            ExprId id = idList.nodes.get(i);
            sbuf.append(String.format("%s = null;", id.toJavaCode()));
        }

        return sbuf.toString();
    }
}
