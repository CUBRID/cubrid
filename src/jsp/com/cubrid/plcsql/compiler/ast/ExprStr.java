package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

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


