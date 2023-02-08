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

import com.cubrid.plcsql.compiler.SemanticError;
import com.cubrid.plcsql.compiler.visitor.AstNodeVisitor;

import com.cubrid.plcsql.compiler.Misc;

public class StmtCursorFetch implements Stmt {

    @Override
    public <R> R accept(AstNodeVisitor<R> visitor) {
        return visitor.visitStmtCursorFetch(this);
    }

    public final ExprId id;
    public final NodeList<ExprId> intoVars;

    public StmtCursorFetch(ExprId id, NodeList<ExprId> intoVars) {
        this.id = id;
        this.intoVars = intoVars;
    }

    @Override
    public String toJavaCode() {
        String setIntoVarsStr = getSetIntoVarsStr(intoVars);
        return tmplStmt.replace("%'CURSOR'%", id.toJavaCode())
                .replace("    %'SET-INTO-VARIABLES'%", Misc.indentLines(setIntoVarsStr, 2));
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplStmt =
            Misc.combineLines(
                    "{ // cursor fetch",
                    "  ResultSet rs = %'CURSOR'%.rs;",
                    "  if (rs == null) {",
                    "    ; // do nothing   TODO: throw an exception?",
                    "  } else if (rs.next()) {",
                    "    %'SET-INTO-VARIABLES'%",
                    "  } else {",
                    "    ; // TODO: what to do? setting nulls to into-variables? ",
                    "  }",
                    "}");

    private static String getSetIntoVarsStr(NodeList<ExprId> intoVars) {

        int i = 0;
        StringBuffer sbuf = new StringBuffer();
        for (ExprId id : intoVars.nodes) {

            if (i > 0) {
                sbuf.append("\n");
            }

            sbuf.append(
                    String.format(
                            "%s = (%s) rs.getObject(%d);",
                            id.toJavaCode(), ((DeclVarLike) id.decl).typeSpec().name, i + 1));

            i++;
        }
        return sbuf.toString();
    }
}
