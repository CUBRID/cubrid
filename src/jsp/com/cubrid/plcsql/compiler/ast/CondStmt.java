package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class CondStmt implements I_Stmt {

    public final I_Expr cond;
    public final NodeList<I_Stmt> stmts;

    public CondStmt(I_Expr cond, NodeList<I_Stmt> stmts) {
        this.cond = cond;
        this.stmts = stmts;
    }

    public CondStmt(I_Expr cond, I_Stmt stmt) {
        this(cond, new NodeList<I_Stmt>().addNode(stmt));
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("%CONDITION%", cond.toJavaCode())
            .replace("  %STATEMENTS%", Misc.indentLines(stmts.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "if (%CONDITION%) {",
        "  %STATEMENTS%",
        "}"
    );
}
