package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtRollback implements I_Stmt {

    @Override
    public String toJavaCode() {
        return "conn.rollback();";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
