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
import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import org.antlr.v4.runtime.ParserRuleContext;
import java.util.ArrayList;

public class StmtCursorOpen extends Stmt {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitStmtCursorOpen(this);
    }

    public final ExprId cursor;
    public final NodeList<Expr> args;

    public StmtCursorOpen(ParserRuleContext ctx, ExprId cursor, NodeList<Expr> args) {
        super(ctx);

        assert cursor.decl instanceof DeclCursor;
        assert args != null;

        this.cursor = cursor;
        this.args = args;
    }

    @Override
    public String toJavaCode() {
        DeclCursor decl = (DeclCursor) cursor.decl;
        String dupCursorArgStr = getDupCursorArgStr(decl.paramRefCounts);
        String hostValuesStr = getHostValuesStr(decl.paramMarks, decl.paramRefCounts);
        return tmplStmt.replace("  %'DUPLICATE-CURSOR-ARG'%", Misc.indentLines(dupCursorArgStr, 1))
                .replace("  %'CURSOR'%", Misc.indentLines(cursor.toJavaCode(), 1))
                .replace("%'HOST-VALUES'%", Misc.indentLines(hostValuesStr, 2, true))
                .replace("%'LEVEL'%", "" + cursor.scope.level);
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplStmt =
            Misc.combineLines(
                    "{ // cursor open",
                    "  %'DUPLICATE-CURSOR-ARG'%",
                    "  %'CURSOR'%.open(conn%'HOST-VALUES'%);",
                    "}");

    // --------------------------------------------------
    // Protected
    // --------------------------------------------------

    protected String getDupCursorArgStr(int[] paramRefCounts) {

        StringBuffer sbuf = new StringBuffer();

        boolean first = true;
        int size = paramRefCounts.length;
        for (int i = 0; i < size; i++) {
            if (paramRefCounts[i] > 1) {
                if (first) {
                    first = false;
                } else {
                    sbuf.append("\n");
                }

                sbuf.append(
                        String.format(
                                "Object a%d_%%'LEVEL'%% = %s;",
                                i, Misc.indentLines(args.nodes.get(i).toJavaCode(), 1, true)));
            }
        }

        if (first) {
            return "// no duplicate cursor parameters";
        } else {
            return sbuf.toString();
        }
    }

    protected String getHostValuesStr(int[] paramMarks, int[] paramRefCounts) {

        int size = paramMarks.length;
        if (size == 0) {
            return "/* no used host values */";
        } else {
            DeclCursor decl = (DeclCursor) cursor.decl;
            StringBuffer sbuf = new StringBuffer();
            ArrayList<ExprId> hostVars = new ArrayList<>(decl.staticSql.hostVars.keySet());
            for (int i = 0; i < size; i++) {
                sbuf.append(",\n");
                int m = paramMarks[i];
                if (m > 0) {
                    int k = m - 1;
                    if (paramRefCounts[k] > 1) {
                        // parameter-k appears more than once in the select statement
                        sbuf.append("a" + k + "_%'LEVEL'%");
                    } else {
                        assert paramRefCounts[k] == 1;
                        sbuf.append(args.nodes.get(k).toJavaCode());
                    }
                } else {
                    ExprId var = hostVars.get(i);
                    assert var.decl != null;
                    var.prefixDeclBlock = var.decl.scope().declDone;
                    sbuf.append(var.toJavaCode());
                }
            }
            return sbuf.toString();
        }
    }
}
