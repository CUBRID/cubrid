package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class StmtNull implements I_Stmt {

    public StmtNull() { }

    @Override
    public String toJavaCode() {
        return ";";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
