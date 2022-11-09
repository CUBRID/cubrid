package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtCursorFetch implements I_Stmt {

    public final ExprId cursor;
    public final NodeList<ExprId> intoVars;

    public StmtCursorFetch(ExprId cursor, NodeList<ExprId> intoVars) {
        this.cursor = cursor;
        this.intoVars = intoVars;
    }

    @Override
    public String toJavaCode() {
        String setIntoVarsStr = getSetIntoVarsStr(intoVars);
        return tmplStmt
            .replace("%CURSOR%", cursor.toJavaCode())
            .replace("    %SET-INTO-VARIABLES%", Misc.indentLines(setIntoVarsStr, 2))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplStmt = Misc.combineLines(
        "{ // cursor fetch",
        "  ResultSet rs = %CURSOR%.rs;",
        "  if (rs == null) {",
        "    ; // do nothing   TODO: throw an exception?",
        "  } else if (rs.next()) {",
        "    %SET-INTO-VARIABLES%",
        "  } else {",
        "    ; // TODO: what to do? setting nulls to into-variables? ",
        "  }",
        "}"
    );

    private static String getSetIntoVarsStr(NodeList<ExprId> intoVars) {

        int i = 0;
        StringBuffer sbuf = new StringBuffer();
        for (ExprId id: intoVars.nodes) {

            if (i > 0) {
                sbuf.append("\n");
            }

            sbuf.append(String.format("%s = (%s) rs.getObject(%d);",
                id.toJavaCode(),
                id.decl.typeSpec().name,
                i + 1));

            i++;
        }
        return sbuf.toString();
    }
}
