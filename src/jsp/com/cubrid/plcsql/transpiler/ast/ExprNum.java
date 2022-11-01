package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprNum implements I_Expr {

    public final String val;

    public ExprNum(String val) {
        this.val = val;
    }

    @Override
    public String toJavaCode() {
        return val;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}


