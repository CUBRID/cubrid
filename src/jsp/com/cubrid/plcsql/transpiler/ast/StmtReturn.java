package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class StmtReturn implements I_Stmt {

    public final I_Expr retVal;

    public StmtReturn(I_Expr retVal) {
        this.retVal = retVal;
    }

    @Override
    public String toJavaCode() {
        if (retVal == null) {
            return "return;";
        } else {
            return "return " + retVal.toJavaCode() + ";";
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
