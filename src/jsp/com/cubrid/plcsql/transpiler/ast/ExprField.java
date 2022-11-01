package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprField implements I_Expr {

    public final ExprId record;
    public String fieldName;

    public ExprField(ExprId record, String fieldName) {
        this.record = record;
        this.fieldName = fieldName;
    }

    @Override
    public String toJavaCode() {
        return record.toJavaCode() + ".getObject(\"" + fieldName + "\")";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
