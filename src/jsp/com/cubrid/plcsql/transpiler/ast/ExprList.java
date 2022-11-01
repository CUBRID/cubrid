package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprList implements I_Expr {

    public final NodeList<I_Expr> elems;

    public ExprList(NodeList<I_Expr> elems) {
        this.elems = elems;
    }

    @Override
    public String toJavaCode() {
        if (elems == null) {
            return "new Object[0]";
        } else {
            return "new Object[] {\n" + Misc.indentLines(elems.toJavaCode(",\n"), 1) + "\n}";
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}


