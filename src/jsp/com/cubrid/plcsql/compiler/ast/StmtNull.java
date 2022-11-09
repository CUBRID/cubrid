package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

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
