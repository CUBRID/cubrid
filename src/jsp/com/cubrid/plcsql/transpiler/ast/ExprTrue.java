package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprTrue implements I_Expr {

    public static ExprTrue instance() {
        return singleton;
    }

    private static ExprTrue singleton = new ExprTrue();
    private ExprTrue() { }

    @Override
    public String toJavaCode() {
        return "true";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}


