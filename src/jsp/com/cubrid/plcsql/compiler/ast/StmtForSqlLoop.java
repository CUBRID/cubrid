package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtForSqlLoop implements I_Stmt {

    public final boolean isDynamic;
    public final int level;
    public final String label;
    public final String record;
    public final I_Expr sql;
    public final NodeList<? extends I_Expr> usedExprList;
    public final NodeList<I_Stmt> stmts;

    public StmtForSqlLoop(boolean isDynamic, int level, String label, String record, I_Expr sql,
            NodeList<? extends I_Expr> usedExprList, NodeList<I_Stmt> stmts) {
        this.isDynamic = isDynamic;
        this.level = level;
        this.label = label;
        this.record = record;
        this.sql = sql;
        this.usedExprList = usedExprList;
        this.stmts = stmts;
    }

    @Override
    public String toJavaCode() {
        String setUsedValuesStr = Common.getSetUsedValuesStr(usedExprList);
        return tmplStmt
            .replace("%KIND%", isDynamic ? "dynamic" : "static")
            .replace("%SQL%", sql.toJavaCode())
            .replace("  %SET-USED-VALUES%", Misc.indentLines(setUsedValuesStr, 1))
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
        "{ // for-loop with %KIND% SQL",
        "  String sql_%LEVEL% = %SQL%;",
        "  PreparedStatement stmt_%LEVEL% = conn.prepareStatement(sql_%LEVEL%);",
        "  %SET-USED-VALUES%",
        "  ResultSet $%RECORD%_r%LEVEL% = stmt_%LEVEL%.executeQuery();",
        "  %LABEL%",
        "  while ($%RECORD%_r%LEVEL%.next()) {",
        "    %STATEMENTS%",
        "  }",
        "  stmt_%LEVEL%.close();",
        "}"
    );

}
