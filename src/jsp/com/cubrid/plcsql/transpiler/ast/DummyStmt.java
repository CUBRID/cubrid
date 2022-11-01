package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class DummyStmt implements I_Stmt {

    public final String kind;

    public DummyStmt(String kind) {
        this.kind = kind;
    }

    @Override
    public String toJavaCode() {
        return "//%TODO-Stmt(" + kind + ")%";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
