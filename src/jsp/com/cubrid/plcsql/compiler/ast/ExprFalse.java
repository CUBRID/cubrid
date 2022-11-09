package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

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


