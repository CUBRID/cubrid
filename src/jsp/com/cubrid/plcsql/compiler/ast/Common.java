package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

class Common {

    static String getSetUsedValuesStr(NodeList<? extends I_Expr> exprList) {
        return getSetUsedValuesStr(exprList, 1);
    }

    static String getSetUsedValuesStr(NodeList<? extends I_Expr> exprList, int startIndex) {

        if (exprList == null || exprList.nodes.size() == 0) {
            return "// no used values";
        }

        StringBuffer sbuf = new StringBuffer();
        int size = exprList.nodes.size();
        for (int i = 0; i < size; i++) {
            if (i > 0) {
                sbuf.append("\n");
            }
            I_Expr expr = exprList.nodes.get(i);
            sbuf.append(tmplSetObject
                .replace("%INDEX%", "" + (i + startIndex))
                .replace("  %VALUE%", Misc.indentLines(expr.toJavaCode(), 1))
                );
        }

        return sbuf.toString();
    }

    static String getQuestionMarks(int n) {
        StringBuffer sbuf = new StringBuffer();
        boolean first = true;
        for (int i = 0; i < n; i++) {

            if (first) {
                first = false;
            } else {
                sbuf.append(", ");
            }

            sbuf.append("?");
        }

        return sbuf.toString();
    }

    // ----------------------------------------------------
    // Private
    // ----------------------------------------------------

    private static final String tmplSetObject = Misc.combineLines(
        "stmt_%LEVEL%.setObject(",
        "  %INDEX%,",
        "  %VALUE%",
        ");"
    );


}

