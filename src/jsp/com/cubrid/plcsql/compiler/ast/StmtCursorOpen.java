package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtCursorOpen implements I_Stmt {

    public final int level;
    public final ExprId cursor;
    public final NodeList<I_Expr> args;

    public StmtCursorOpen(int level, ExprId cursor, NodeList<I_Expr> args) {
        assert cursor.decl instanceof DeclCursor;

        this.level = level;
        this.cursor = cursor;
        this.args = args;
    }

    @Override
    public String toJavaCode() {
        DeclCursor decl = (DeclCursor) cursor.decl;
        String dupCursorArgStr = getDupCursorArgStr(decl.paramRefCounts);
        String hostValuesStr = getHostValuesStr(decl.usedValuesMap, decl.paramRefCounts);
        return tmplStmt
            .replace("  %DUPLICATE-CURSOR-ARG%", Misc.indentLines(dupCursorArgStr , 1))
            .replace("  %CURSOR%", Misc.indentLines(cursor.toJavaCode(), 1))
            .replace("%HOST-VALUES%", Misc.indentLines(hostValuesStr, 2, true))
            .replace("%LEVEL%", "" + level)
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplStmt = Misc.combineLines(
        "{ // cursor open",
        "  %DUPLICATE-CURSOR-ARG%",
        "  %CURSOR%.open(conn%HOST-VALUES%);",
        "}"
    );

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

                sbuf.append(String.format("Object a%d_%%LEVEL%% = %s;", i,
                    Misc.indentLines(args.nodes.get(i).toJavaCode(), 1, true)));
            }
        }

        if (first) {
            return "// no duplicate cursor parameters";
        } else {
            return sbuf.toString();
        }
    }

    protected String getHostValuesStr(int[] usedValuesMap, int[] paramRefCounts) {

        int size = usedValuesMap.length;
        if (size == 0) {
            return "/* no used host values */";
        } else {
            DeclCursor decl = (DeclCursor) cursor.decl;
            StringBuffer sbuf = new StringBuffer();
            for (int i = 0; i < size; i++) {
                sbuf.append(",\n");
                int m = usedValuesMap[i];
                if (m < 0) {
                    int k = -m - 1;
                    if (paramRefCounts[k] > 1) {
                        // parameter-k appears more than one times in the select statement
                        sbuf.append("a" + k + "_%LEVEL%");
                    } else {
                        assert paramRefCounts[k] == 1;
                        sbuf.append(args.nodes.get(k).toJavaCode());
                    }
                } else {
                    sbuf.append(decl.usedVars.nodes.get(i).toJavaCode());
                }
            }
            return sbuf.toString();
        }
    }
}
