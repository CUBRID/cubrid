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

import com.cubrid.plcsql.compiler.Coercion;
import com.cubrid.plcsql.compiler.Misc;
import java.util.List;
import org.antlr.v4.runtime.ParserRuleContext;

public abstract class StmtSql extends Stmt {

    public final boolean dynamic;
    public final int level;
    public final Expr sql;
    public final List<TypeSpec> columnTypeList;
    public final List<ExprId> intoVarList;
    public final List<? extends Expr> usedExprList;

    public StmtSql(
            ParserRuleContext ctx,
            boolean dynamic,
            int level,
            Expr sql,
            List<TypeSpec> columnTypeList,
            List<ExprId> intoVarList,
            List<? extends Expr> usedExprList) {
        super(ctx);

        // if static and a SELECT statement, then columnTypeList must be given
        assert dynamic || intoVarList == null || columnTypeList != null;

        this.dynamic = dynamic;
        this.level = level;
        this.sql = sql;
        this.columnTypeList = columnTypeList;
        this.intoVarList = intoVarList;
        this.usedExprList = usedExprList;
    }

    @Override
    public String toJavaCode() {
        String setUsedExprStr = Common.getSetUsedExprStr(usedExprList);

        if (intoVarList == null) {
            assert coercions == null;

            return tmplDml.replace("%'KIND'%", dynamic ? "dynamic" : "static")
                    .replace("%'SQL'%", Misc.indentLines(sql.toJavaCode(), 1, true))
                    .replace("  %'SET-USED-VALUES'%", Misc.indentLines(setUsedExprStr, 1))
                    .replace("%'LEVEL'%", "" + level);
        } else {
            assert coercions != null;

            String setResultsStr = getSetResultsStr(intoVarList);
            return tmplSelect
                    .replace("%'KIND'%", dynamic ? "dynamic" : "static")
                    .replace("%'SQL'%", Misc.indentLines(sql.toJavaCode(), 1, true))
                    .replace("  %'SET-USED-VALUES'%", Misc.indentLines(setUsedExprStr, 1))
                    .replace("      %'SET-RESULTS'%", Misc.indentLines(setResultsStr, 3))
                    .replace("%'LEVEL'%", "" + level);
        }
    }

    public void setCoercions(List<Coercion> coercions) {
        this.coercions = coercions;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private List<Coercion> coercions;

    private static final String tmplDml =
            Misc.combineLines(
                    "try { // %'KIND'% SQL statement",
                    "  String dynSql_%'LEVEL'% = %'SQL'%;",
                    "  PreparedStatement stmt_%'LEVEL'% = conn.prepareStatement(dynSql_%'LEVEL'%);",
                    "  %'SET-USED-VALUES'%",
                    "  sql_rowcount[0] = (long) stmt_%'LEVEL'%.executeUpdate();",
                    "  stmt_%'LEVEL'%.close();",
                    "} catch (SQLException e) {",
                    "  Server.log(e);",
                    "  throw new SQL_ERROR(e.getMessage());",
                    "}");

    private static final String tmplSelect =
            Misc.combineLines(
                    "try { // %'KIND'% Select statement",
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
                    "    sql_rowcount[0] = 0L;",
                    "    throw new NO_DATA_FOUND();",
                    "  } else if (i%'LEVEL'% == 1) {",
                    "    sql_rowcount[0] = 1L;",
                    "  } else {",
                    "    sql_rowcount[0] = 1L; // Surprise? Refer to the Spec.",
                    "    throw new TOO_MANY_ROWS();",
                    "  }",
                    "  stmt_%'LEVEL'%.close();",
                    "} catch (SQLException e) {",
                    "  Server.log(e);",
                    "  throw new SQL_ERROR(e.getMessage());",
                    "}");

    private String getSetResultsStr(List<ExprId> intoVarList) {

        StringBuffer sbuf = new StringBuffer();

        int size = intoVarList.size();
        assert coercions.size() == size;
        assert dynamic || (columnTypeList != null && columnTypeList.size() == size);

        int i = 0;
        for (ExprId id : intoVarList) {

            assert id.decl instanceof DeclVar || id.decl instanceof DeclParamOut
                    : "only variables or out-parameters can be used in into-clauses";

            String nameOfGetMethod =
                    dynamic ? "getObject" : columnTypeList.get(i).getNameOfGetMethod();
            String resultStr = String.format("r%%'LEVEL'%%.%s(%d)", nameOfGetMethod, i + 1);

            if (i > 0) {
                sbuf.append("\n");
            }

            Coercion c = coercions.get(i);
            sbuf.append(String.format("%s = %s;", id.toJavaCode(), c.toJavaCode(resultStr)));

            i++;
        }

        return sbuf.toString();
    }
}
