package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class CaseStmt implements I_Stmt {

    public final I_Expr val;
    public final NodeList<I_Stmt> stmts;

    public CaseStmt(I_Expr val, NodeList<I_Stmt> stmts) {
        this.val = val;
        this.stmts = stmts;
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("%VALUE%", val.toJavaCode())
            .replace("  %STATEMENTS%", Misc.indentLines(stmts.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "if (Objects.equals(selector_%LEVEL%, %VALUE%)) {",
        "  %STATEMENTS%",
        "}"
    );
}
