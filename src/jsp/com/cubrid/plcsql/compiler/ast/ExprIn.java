package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class ExprIn implements I_Expr {

    public final I_Expr target;
    public final NodeList<I_Expr> inElements;

    public ExprIn(I_Expr target, NodeList<I_Expr> inElements) {
        this.target = target;
        this.inElements = inElements;
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("  %TARGET%", Misc.indentLines(target.toJavaCode(), 1))
            .replace("  %IN-ELEMENTS%", Misc.indentLines(inElements.toJavaCode(",\n"), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "opIn(",
        "  %TARGET%,",
        "  %IN-ELEMENTS%",
        ")"
    );

}
