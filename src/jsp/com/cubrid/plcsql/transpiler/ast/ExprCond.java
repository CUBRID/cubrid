package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprCond implements I_Expr {

    public final NodeList<CondExpr> condParts;
    public final I_Expr elsePart;

    public ExprCond(NodeList<CondExpr> condParts, I_Expr elsePart) {
        this.condParts = condParts;
        this.elsePart = elsePart;
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("%COND-PARTS%", condParts.toJavaCode())
            .replace("%ELSE-PART%", elsePart == null ? "null" : elsePart.toJavaCode())
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "(%COND-PARTS%",
        "%ELSE-PART%)"
    );
}
