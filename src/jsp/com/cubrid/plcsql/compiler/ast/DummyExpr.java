package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class DummyExpr implements I_Expr {

    public final String kind;

    public DummyExpr(String kind) {
        this.kind = kind;
    }

    @Override
    public String toJavaCode() {
        return "%TODO-Expr(" + kind + ")%";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
