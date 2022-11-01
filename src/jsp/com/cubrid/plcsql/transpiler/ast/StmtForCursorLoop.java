package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

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
