package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprNull implements I_Expr {

    public static ExprNull instance() {
        return singleton;
    }

    private static ExprNull singleton = new ExprNull();
    private ExprNull() { }

    @Override
    public String toJavaCode() {
        return "null";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}


