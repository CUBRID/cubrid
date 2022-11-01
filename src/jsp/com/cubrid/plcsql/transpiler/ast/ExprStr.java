package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprStr implements I_Expr {

    public final String val;

    public ExprStr(String val) {
        this.val = val;
    }

    @Override
    public String toJavaCode() {
        return '"' + val + '"';     // TODO: do I have to escape val?
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}


