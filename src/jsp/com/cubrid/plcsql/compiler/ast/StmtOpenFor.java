package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

import java.util.Arrays;

public class StmtOpenFor implements I_Stmt {

    public final ExprId refCursor;
    public final ExprStr sql;
    public final NodeList<ExprId> usedVars;

    public StmtOpenFor(ExprId refCursor, ExprStr sql, NodeList<ExprId> usedVars) {
        this.refCursor = refCursor;
        this.sql = sql;
        this.usedVars = usedVars;
    }

    @Override
    public String toJavaCode() {
        return tmplStmt
            .replace("%REF-CURSOR%", refCursor.toJavaCode())
            .replace("%QUERY%", sql.toJavaCode())
            .replace("    %HOST-VARIABLES%", Misc.indentLines(usedVars.toJavaCode(",\n"), 2))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplStmt = Misc.combineLines(
        "{ // open-for statement",
        "  %REF-CURSOR% = new Query(%QUERY%);",
        "  %REF-CURSOR%.open(conn,",
        "    %HOST-VARIABLES%",
        "  );",
        "}"
    );
}
