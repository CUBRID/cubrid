package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class StmtCursorClose implements I_Stmt {

    public final ExprId cursor;

    public StmtCursorClose(ExprId cursor) {
        this.cursor = cursor;
    }

    @Override
    public String toJavaCode() {
        return cursor.toJavaCode() + ".close();";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
