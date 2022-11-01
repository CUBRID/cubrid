package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class StmtRollback implements I_Stmt {

    @Override
    public String toJavaCode() {
        return "conn.rollback();";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
