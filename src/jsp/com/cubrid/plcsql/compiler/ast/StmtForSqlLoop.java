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

import org.antlr.v4.runtime.ParserRuleContext;

import com.cubrid.plcsql.compiler.SemanticError;
import com.cubrid.plcsql.compiler.visitor.AstVisitor;

import com.cubrid.plcsql.compiler.Misc;

public class StmtForSqlLoop extends Stmt {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitStmtForSqlLoop(this);
    }

    public final boolean isDynamic;
    public final String label;
    public final DeclForRecord record;
    public final Expr sql;
    public final NodeList<? extends Expr> usedExprList;
    public final NodeList<Stmt> stmts;

    public StmtForSqlLoop(ParserRuleContext ctx,
            boolean isDynamic,
            String label,
            DeclForRecord record,
            Expr sql,
            NodeList<? extends Expr> usedExprList,
            NodeList<Stmt> stmts) {
        super(ctx);

        this.isDynamic = isDynamic;
        this.label = label;
        this.record = record;
        this.sql = sql;
        this.usedExprList = usedExprList;
        this.stmts = stmts;
    }

    @Override
    public String toJavaCode() {
        String setUsedValuesStr = Common.getSetUsedValuesStr(usedExprList);
        return tmplStmt.replace("%'KIND'%", isDynamic ? "dynamic" : "static")
                .replace("%'SQL'%", sql.toJavaCode())
                .replace("  %'SET-USED-VALUES'%", Misc.indentLines(setUsedValuesStr, 1))
                .replace("%'RECORD'%", record.name)
                .replace("%'LABEL'%", label == null ? "// no label" : label + "_%'LEVEL'%:")
                .replace("%'LEVEL'%", "" + record.scope.level)
                .replace("    %'STATEMENTS'%", Misc.indentLines(stmts.toJavaCode(), 2));
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplStmt =
            Misc.combineLines(
                    "{ // for-loop with %'KIND'% SQL",
                    "  String sql_%'LEVEL'% = %'SQL'%;",
                    "  PreparedStatement stmt_%'LEVEL'% = conn.prepareStatement(sql_%'LEVEL'%);",
                    "  %'SET-USED-VALUES'%",
                    "  ResultSet %'RECORD'%_r%'LEVEL'% = stmt_%'LEVEL'%.executeQuery();",
                    "  %'LABEL'%",
                    "  while (%'RECORD'%_r%'LEVEL'%.next()) {",
                    "    %'STATEMENTS'%",
                    "  }",
                    "  stmt_%'LEVEL'%.close();",
                    "}");
}
