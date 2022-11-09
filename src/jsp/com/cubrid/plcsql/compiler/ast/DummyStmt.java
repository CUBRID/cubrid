package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

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
