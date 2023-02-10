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

public class StmtOpenFor extends Stmt {

    @Override
    public <R> R accept(AstNodeVisitor<R> visitor) {
        return visitor.visitStmtOpenFor(this);
    }

    public final ExprId id;
    public final ExprStr sql;
    public final NodeList<ExprId> usedVars;

    public StmtOpenFor(ExprId id, ExprStr sql, NodeList<ExprId> usedVars) {
        this.id = id;
        this.sql = sql;
        this.usedVars = usedVars;
    }

    @Override
    public String toJavaCode() {
        return tmplStmt.replace("%'REF-CURSOR'%", id.toJavaCode())
                .replace("%'QUERY'%", sql.toJavaCode())
                .replace("    %'HOST-VARIABLES'%", Misc.indentLines(usedVars.toJavaCode(",\n"), 2));
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplStmt =
            Misc.combineLines(
                    "{ // open-for statement",
                    "  %'REF-CURSOR'% = new Query(%'QUERY'%);",
                    "  %'REF-CURSOR'%.open(conn,",
                    "    %'HOST-VARIABLES'%",
                    "  );",
                    "}");
}
