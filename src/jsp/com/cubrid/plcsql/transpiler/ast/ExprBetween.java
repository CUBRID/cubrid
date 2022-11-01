package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprBetween implements I_Expr {

    public final I_Expr target;
    public final I_Expr lowerBound;
    public final I_Expr upperBound;

    public ExprBetween(I_Expr target, I_Expr lowerBound, I_Expr upperBound) {
        this.target = target;
        this.lowerBound = lowerBound;
        this.upperBound = upperBound;
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("  %TARGET%", Misc.indentLines(target.toJavaCode(), 1))
            .replace("  %LOWER-BOUND%", Misc.indentLines(lowerBound.toJavaCode(), 1))
            .replace("  %UPPER-BOUND%", Misc.indentLines(upperBound.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "opBetween(",
        "  %TARGET%,",
        "  %LOWER-BOUND%,",
        "  %UPPER-BOUND%",
        ")"
    );

}
