package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprGlobalFuncCall implements I_Expr {

    public final int level;
    public final String name;
    public final NodeList<I_Expr> args;

    public ExprGlobalFuncCall(int level, String name, NodeList<I_Expr> args) {
        this.level = level;
        this.name = name;
        this.args = args;
    }

    @Override
    public String toJavaCode() {

        int argSize = args.nodes.size();
        String dynSql = getDynSql(name, argSize);
        String paramStr = getParametersStr(argSize);
        String setUsedValuesStr = getSetUsedValuesStr(argSize);

        return tmplStmt
            .replace("%FUNC-NAME%", name)
            .replace("%DYNAMIC-SQL%", dynSql)
            .replace("%PARAMETERS%", paramStr)
            .replace("    %SET-USED-VALUES%", Misc.indentLines(setUsedValuesStr, 2))
            .replace("  %ARGUMENTS%", Misc.indentLines(args.toJavaCode(",\n"), 1))
            .replace("%LEVEL%", "" + level)
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplStmt = Misc.combineLines(
        "(new Object() { // global function call: %FUNC-NAME%",
        "  Object invoke(%PARAMETERS%) throws Exception {",
        "    String dynSql_%LEVEL% = \"%DYNAMIC-SQL%\";",
        "    CallableStatement stmt_%LEVEL% = conn.prepareCall(dynSql_%LEVEL%);",
        "    stmt_%LEVEL%.registerOutParameter(1, java.sql.Types.OTHER);",
        "    %SET-USED-VALUES%",
        "    stmt_%LEVEL%.execute();",
        "    Object ret_%LEVEL% = stmt_%LEVEL%.getObject(1);",
        "    stmt_%LEVEL%.close();",
        "    return ret_%LEVEL%;",
        "  }",
        "}.invoke(",
        "  %ARGUMENTS%",
        "))"
    );

    private static String getDynSql(String name, int argCount) {
        return String.format("?= call %s(%s)", name, Common.getQuestionMarks(argCount));
    }

    private static String getParametersStr(int size) {

        StringBuffer sbuf = new StringBuffer();
        boolean first = true;
        for (int i = 0; i < size; i++) {
            if (first) {
                first = false;
            } else {
                sbuf.append(", ");
            }

            sbuf.append("Object o" + i);
        }

        return sbuf.toString();
    }

    private static String getSetUsedValuesStr(int size) {

        StringBuffer sbuf = new StringBuffer();
        boolean first = true;
        for (int i = 0; i < size; i++) {
            if (first) {
                first = false;
            } else {
                sbuf.append(";\n");
            }

            sbuf.append(String.format("stmt_%%LEVEL%%.setObject(%d, o%d);", i + 2, i));
        }

        return sbuf.toString();
    }
}
