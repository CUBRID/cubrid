/*
 * Copyright (C) 2008 Search Solution Corporation.
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

public class StmtForCursorLoop extends StmtCursorOpen {

    //public final int level;
    //public final ExprId cursor;
    //public final NodeList<I_Expr> args;
    public final String label;
    public final String record;
    public final NodeList<I_Stmt> stmts;


    public StmtForCursorLoop(int level, ExprId cursor, NodeList<I_Expr> args,
            String label, String record, NodeList<I_Stmt> stmts) {

        super(level, cursor, args);

        this.label = label;
        this.record = record;
        this.stmts = stmts;
    }

    @Override
    public String toJavaCode() {
        DeclCursor decl = (DeclCursor) cursor.decl;
        String dupCursorArgStr = getDupCursorArgStr(decl.paramRefCounts);
        String hostValuesStr = getHostValuesStr(decl.usedValuesMap, decl.paramRefCounts);
        return tmplStmt
            .replace("  %DUPLICATE-CURSOR-ARG%", Misc.indentLines(dupCursorArgStr , 1))
            .replace("%CURSOR%", cursor.toJavaCode())
            .replace("%HOST-VALUES%", Misc.indentLines(hostValuesStr, 2, true))
            .replace("%RECORD%", record)
            .replace("%LABEL%", label == null ? "// no label" : label + "_%LEVEL%:")
            .replace("%LEVEL%", "" + level)
            .replace("    %STATEMENTS%", Misc.indentLines(stmts.toJavaCode(), 2))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplStmt = Misc.combineLines(
        "{ // for loop with a cursor",
        "  %DUPLICATE-CURSOR-ARG%",
        "  %CURSOR%.open(conn%HOST-VALUES%);",
        "  ResultSet $%RECORD%_r%LEVEL% = %CURSOR%.rs;",
        "  %LABEL%",
        "  while ($%RECORD%_r%LEVEL%.next()) {",
        "    %STATEMENTS%",
        "  }",
        "  %CURSOR%.close();",
        "}"
    );

}
