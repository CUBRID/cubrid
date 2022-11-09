package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class ExprCast implements I_Expr {

    public final I_Expr expr;
    public String ty = null;

    public ExprCast(I_Expr expr) {
        this.expr = expr;
    }

    public void setTargetType(String ty) {
        assert ty != null;

        if (this.ty == null) {
            this.ty = ty;
        } else {
            assert false: "target type of an ExprCast is set to " + ty + " with already set " + this.ty;
        }
    }

    @Override
    public String toJavaCode() {
        if (ty == null) {
            return String.format("((%%TODO-ExprCast%%) (%s))", Misc.indentLines(expr.toJavaCode(), 1, true));
        } else {
            return String.format("((%s) (%s))", ty, Misc.indentLines(expr.toJavaCode(), 1, true));
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
