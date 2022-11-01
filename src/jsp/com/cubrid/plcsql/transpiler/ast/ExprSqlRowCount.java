package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;
import com.cubrid.plcsql.transpiler.Scope;

public class ExprSqlRowCount implements I_Expr {

    @Override
    public String toJavaCode() {
        return "sql_rowcount[0]";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
