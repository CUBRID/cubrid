package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class CondExpr implements I_Expr {

    public final I_Expr cond;
    public final I_Expr expr;

    public CondExpr(I_Expr cond, I_Expr expr) {
        this.cond = cond;
        this.expr = expr;
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("%CONDITION%", cond.toJavaCode())
            .replace("  %EXPRESSION%", Misc.indentLines(expr.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "%CONDITION% ?",
        "  %EXPRESSION% :"
    );
}
