package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprFalse implements I_Expr {

    public static ExprFalse instance() {
        return singleton;
    }

    private static ExprFalse singleton = new ExprFalse();
    private ExprFalse() { }

    @Override
    public String toJavaCode() {
        return "false";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}


