package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.Scope;

public class ExprSqlRowCount implements I_Expr {

    @Override
    public String toJavaCode() {
        return "sql_rowcount[0]";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
