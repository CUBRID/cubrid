package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

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
