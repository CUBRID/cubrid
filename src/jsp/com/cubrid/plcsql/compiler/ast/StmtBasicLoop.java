package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtBasicLoop implements I_Stmt {

    public final DeclLabel declLabel;
    public final NodeList<I_Stmt> stmts;

    public StmtBasicLoop(DeclLabel declLabel, NodeList<I_Stmt> stmts) {
        this.declLabel = declLabel;
        this.stmts = stmts;
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("%OPT-LABEL%", declLabel == null ? "// no label": declLabel.toJavaCode())
            .replace("  %STATEMENTS%", Misc.indentLines(stmts.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "%OPT-LABEL%",
        "while (true) {",
        "  %STATEMENTS%",
        "}"
    );
}

