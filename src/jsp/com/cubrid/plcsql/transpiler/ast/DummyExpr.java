package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

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
