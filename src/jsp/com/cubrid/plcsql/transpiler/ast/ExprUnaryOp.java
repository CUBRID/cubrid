package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprUnaryOp implements I_Expr {

    public final String opStr;
    public final I_Expr o;

    public ExprUnaryOp(String opStr, I_Expr o) {
        this.opStr = opStr;
        this.o = o;
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("%OPERATION%", opStr)
            .replace("  %OPERAND%", Misc.indentLines(o.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "op%OPERATION%(",
        "  %OPERAND%",
        ")"
    );

}
