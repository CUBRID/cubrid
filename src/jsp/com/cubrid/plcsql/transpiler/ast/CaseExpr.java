package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class CaseExpr implements I_Stmt {

    public final I_Expr val;
    public final I_Expr expr;

    public CaseExpr(I_Expr val, I_Expr expr) {
        this.val = val;
        this.expr = expr;
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("%VALUE%", val.toJavaCode())
            .replace("  %EXPRESSION%", Misc.indentLines(expr.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "Objects.equals(selector_%LEVEL%, %VALUE%) ?",
        "  %EXPRESSION% :"
    );
}
