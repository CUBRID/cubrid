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

import com.cubrid.plcsql.compiler.Coerce;
import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import java.util.List;
import org.antlr.v4.runtime.ParserRuleContext;

public class StmtCursorFetch extends Stmt {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitStmtCursorFetch(this);
    }

    public final ExprId id;
    public final List<TypeSpec> columnTypeList;
    public final List<ExprId> intoVarList;

    public StmtCursorFetch(
            ParserRuleContext ctx,
            ExprId id,
            List<TypeSpec> columnTypeList,
            List<ExprId> intoVarList) {
        super(ctx);

        this.id = id;
        this.columnTypeList = columnTypeList;
        this.intoVarList = intoVarList;
    }

    @Override
    public String toJavaCode() {
        String setIntoVarsStr = getSetIntoVarsStr(intoVarList);
        return tmplStmt.replace("%'CURSOR'%", id.toJavaCode())
                .replace("    %'SET-INTO-VARIABLES'%", Misc.indentLines(setIntoVarsStr, 2));
    }

    public void setCoerces(List<Coerce> coerces) {
        this.coerces = coerces;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private List<Coerce> coerces;

    private static final String tmplStmt =
            Misc.combineLines(
                    "{ // cursor fetch",
                    "  ResultSet rs = %'CURSOR'%.rs;",
                    "  if (rs == null) {",
                    "    throw new PROGRAM_ERROR();",
                    "  } else if (rs.next()) {",
                    "    %'SET-INTO-VARIABLES'%",
                    "  } else {",
                    "    ; // TODO: what to do? setting nulls to into-variables?",
                    "  }",
                    "}");

    private String getSetIntoVarsStr(List<ExprId> intoVarList) {

        assert coerces != null;
        assert coerces.size() == intoVarList.size();

        int i = 0;
        StringBuffer sbuf = new StringBuffer();
        for (ExprId id : intoVarList) {

            String nameOfGetMethod =
                    (columnTypeList == null)
                            ? "getObject"
                            : columnTypeList.get(i).getNameOfGetMethod();
            String resultStr = String.format("rs.%s(%d)", nameOfGetMethod, i + 1);

            if (i > 0) {
                sbuf.append("\n");
            }

            Coerce c = coerces.get(i);
            sbuf.append(String.format("%s = %s;", id.toJavaCode(), c.toJavaCode(resultStr)));

            i++;
        }
        return sbuf.toString();
    }
}
