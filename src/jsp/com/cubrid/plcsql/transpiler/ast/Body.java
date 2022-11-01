package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class Body implements AstNode {

    public final NodeList<I_Stmt> stmts;
    public final NodeList<ExHandler> exHandlers;

    public Body(NodeList<I_Stmt> stmts, NodeList<ExHandler> exHandlers) {
        this.stmts = stmts;
        this.exHandlers = exHandlers;
    }

    @Override
    public String toJavaCode() {
        if (exHandlers.nodes.size() == 0) {
            return stmts.toJavaCode();
        } else {
            return tmpl
                .replace("  %STATEMENTS%", Misc.indentLines(stmts.toJavaCode(), 1))
                .replace("%CATCHES%", exHandlers.toJavaCode(null))
                ;
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "try {",
        "  %STATEMENTS%",
        "}%CATCHES%"
    );
}


