package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtWhileLoop implements I_Stmt {

    public final DeclLabel declLabel;
    public final I_Expr expr;
    public final NodeList<I_Stmt> stmts;

    public StmtWhileLoop(DeclLabel declLabel, I_Expr expr, NodeList<I_Stmt> stmts) {
        this.declLabel = declLabel;
        this.expr = expr;
        this.stmts = stmts;
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("%OPT-LABEL%", declLabel == null ? "// no label": declLabel.toJavaCode())
            .replace("%EXPRESSION%", expr.toJavaCode())
            .replace("  %STATEMENTS%", Misc.indentLines(stmts.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "%OPT-LABEL%",
        "while (%EXPRESSION%) {",
        "  %STATEMENTS%",
        "}"
    );
}
