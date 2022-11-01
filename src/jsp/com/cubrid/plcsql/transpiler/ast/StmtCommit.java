package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class StmtCommit implements I_Stmt {

    @Override
    public String toJavaCode() {
        return "conn.commit();";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
