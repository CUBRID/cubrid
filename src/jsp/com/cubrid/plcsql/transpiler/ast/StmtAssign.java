package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class StmtAssign implements I_Stmt {

    public final ExprId target;
    public final I_Expr val;

    public StmtAssign(ExprId target, I_Expr val) {
        this.target = target;
        this.val = val;
    }

    @Override
    public String toJavaCode() {
        return String.format("%s = %s;", target.toJavaCode(), val.toJavaCode());
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
