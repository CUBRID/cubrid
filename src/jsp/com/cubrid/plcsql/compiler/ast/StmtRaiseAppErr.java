package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtRaiseAppErr implements I_Stmt {

    public final I_Expr errCode;
    public final I_Expr errMsg;

    public StmtRaiseAppErr(I_Expr errCode, I_Expr errMsg) {
        this.errCode = errCode;
        this.errMsg = errMsg;
    }

    @Override
    public String toJavaCode() {
        return String.format("throw new $$APP_ERROR(%s, %s);", errCode.toJavaCode(), errMsg.toJavaCode());
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
