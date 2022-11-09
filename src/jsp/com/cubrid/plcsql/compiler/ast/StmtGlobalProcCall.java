package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtGlobalProcCall implements I_Stmt {

    public final int level;
    public final String name;
    public final NodeList<I_Expr> args;

    public StmtGlobalProcCall(int level, String name, NodeList<I_Expr> args) {
        this.level = level;
        this.name = name;
        this.args = args;
    }

    @Override
    public String toJavaCode() {
        String dynSql = getDynSql(name, args == null ? 0 : args.nodes.size());
        String setUsedValuesStr = Common.getSetUsedValuesStr(args);
        return tmplStmt
            .replace("%PROC-NAME%", name)
            .replace("%DYNAMIC-SQL%", dynSql)
            .replace("  %SET-USED-VALUES%", Misc.indentLines(setUsedValuesStr, 1))
            .replace("%LEVEL%", "" + level)
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplStmt = Misc.combineLines(
        "{ // global procedure call: %PROC-NAME%",
        "  String dynSql_%LEVEL% = \"%DYNAMIC-SQL%\";",
        "  CallableStatement stmt_%LEVEL% = conn.prepareCall(dynSql_%LEVEL%);",
        "  %SET-USED-VALUES%",
        "  stmt_%LEVEL%.execute();",
        "  stmt_%LEVEL%.close();",
        "}"
    );

    private static String getDynSql(String name, int argCount) {
        return String.format("call %s(%s)", name, Common.getQuestionMarks(argCount));
    }
}
