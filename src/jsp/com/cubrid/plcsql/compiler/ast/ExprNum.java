package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

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


