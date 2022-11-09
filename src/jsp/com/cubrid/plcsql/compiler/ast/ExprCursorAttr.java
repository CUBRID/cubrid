package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class ExprCursorAttr implements I_Expr {

    public final ExprId cursor;
    public final String attribute;

    public ExprCursorAttr(ExprId cursor, String attribute) {
        this.cursor = cursor;
        this.attribute = attribute;
    }

    @Override
    public String toJavaCode() {
        return cursor.toJavaCode() + "." + attribute + "()";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
