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

        String strHandleIntoClause, strBanIntoClause;
        if (intoVarList == null) {
            assert coercions == null;
            strHandleIntoClause = strBanIntoClause = "// no INTO clause";
        } else {
            assert coercions != null;
            String strSetResults = getSetResultsStr(intoVarList);
            strHandleIntoClause =
                    tmplHandleIntoClause.replace(
                            "    %'SET-RESULTS'%", Misc.indentLines(strSetResults, 2));
            strBanIntoClause = tmplBanIntoClause;
        }

        return tmplStmt.replace("%'KIND'%", dynamic ? "dynamic" : "static")
                .replace("%'SQL'%", Misc.indentLines(sql.toJavaCode(), 2, true))
                .replace("    %'BAN-INTO-CLAUSE'%", Misc.indentLines(strBanIntoClause, 2))
                .replace("    %'SET-USED-VALUES'%", Misc.indentLines(setUsedExprStr, 2))
                .replace("      %'HANDLE-INTO-CLAUSE'%", Misc.indentLines(strHandleIntoClause, 3))
                .replace("%'LEVEL'%", "" + level);
    }

    public void setCoercions(List<Coercion> coercions) {
        this.coercions = coercions;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private List<Coercion> coercions;

    private static final String tmplStmt =
            Misc.combineLines(
                    "{ // %'KIND'% SQL statement",
                    "  PreparedStatement stmt_%'LEVEL'% = null;",
                    "  try {",
                    "    String dynSql_%'LEVEL'% = %'SQL'%;",
                    "    stmt_%'LEVEL'% = conn.prepareStatement(dynSql_%'LEVEL'%);",
                    "    %'BAN-INTO-CLAUSE'%",
                    "    %'SET-USED-VALUES'%",
                    "    if (stmt_%'LEVEL'%.execute()) {",
                    "      sql_rowcount[0] = 0L;", // not from the Oracle specification, but from
                    // Oracle 19.0.0.0 behavior
                    "      %'HANDLE-INTO-CLAUSE'%",
                    "    } else {",
                    "      sql_rowcount[0] = (long) stmt_%'LEVEL'%.getUpdateCount();",
                    "    }",
                    "  } catch (SQLException e) {",
                    "    Server.log(e);",
                    "    throw new SQL_ERROR(e.getMessage());",
                    "  } finally {",
                    "    if (stmt_%'LEVEL'% != null) {",
                    "      stmt_%'LEVEL'%.close();",
                    "    }",
                    "  }",
                    "}");

    private static final String tmplHandleIntoClause =
            Misc.combineLines(
                    "ResultSet r%'LEVEL'% = stmt_%'LEVEL'%.getResultSet();",
                    "if (r%'LEVEL'% == null) {",
                    "  throw new SQL_ERROR(\"no result set\");", // EXECUTE IMMEDIATE 'CALL ...'
                    // INTO ... leads to this line
                    "}",
                    "int i%'LEVEL'% = 0;",
                    "while (r%'LEVEL'%.next()) {",
                    "  i%'LEVEL'%++;",
                    "  if (i%'LEVEL'% > 1) {",
                    "    break;",
                    "  } else {",
                    "    %'SET-RESULTS'%",
                    "  }",
                    "}",
                    "if (i%'LEVEL'% == 0) {",
                    "  throw new NO_DATA_FOUND();",
                    "} else if (i%'LEVEL'% == 1) {",
                    "  sql_rowcount[0] = 1L;",
                    "} else {",
                    "  sql_rowcount[0] = 1L; // Surprise? Refer to the Spec.",
                    "  throw new TOO_MANY_ROWS();",
                    "}");

    private static final String tmplBanIntoClause =
            Misc.combineLines(
                    "ResultSetMetaData rsmd_%'LEVEL'% = stmt_%'LEVEL'%.getMetaData();",
                    "if (rsmd_%'LEVEL'% == null || rsmd_%'LEVEL'%.getColumnCount() < 1) {",
                    "  throw new SQL_ERROR(\"INTO clause cannot be used without a SELECT statement\");",
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

            String resultStr;
            if (dynamic) {
                resultStr = String.format("r%%'LEVEL'%%.getObject(%d)", i + 1);
            } else {
                resultStr =
                        String.format(
                                "(%s) r%%'LEVEL'%%.getObject(%d)",
                                columnTypeList.get(i).toJavaCode(), i + 1);
            }

            if (i > 0) {
                sbuf.append("\n");
            }

            Coercion c = coercions.get(i);
            boolean checkNotNull = (id.decl instanceof DeclVar) && ((DeclVar) id.decl).notNull;
            if (checkNotNull) {
                sbuf.append(
                        String.format(
                                "%s = checkNotNull(%s);",
                                id.toJavaCode(), c.toJavaCode(resultStr)));
            } else {
                sbuf.append(String.format("%s = %s;", id.toJavaCode(), c.toJavaCode(resultStr)));
            }

            i++;
        }

        return sbuf.toString();
    }
}
