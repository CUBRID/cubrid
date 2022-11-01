package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprBinaryOp implements I_Expr {

    public final String opStr;
    public final I_Expr left;
    public final I_Expr right;

    public ExprBinaryOp(String opStr, I_Expr left, I_Expr right) {
        this.opStr = opStr;
        this.left = left;
        this.right = right;
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("%OPERATION%", opStr)
            .replace("  %LEFT-OPERAND%", Misc.indentLines(left.toJavaCode(), 1))
            .replace("  %RIGHT-OPERAND%", Misc.indentLines(right.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "op%OPERATION%(",
        "  %LEFT-OPERAND%,",
        "  %RIGHT-OPERAND%",
        ")"
    );

}
