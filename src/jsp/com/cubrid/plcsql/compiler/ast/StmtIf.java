package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtIf implements I_Stmt {

    public final NodeList<CondStmt> condStmtParts;
    public final NodeList<I_Stmt> elsePart;

    public StmtIf(NodeList<CondStmt> condStmtParts, NodeList<I_Stmt> elsePart) {
        this.condStmtParts = condStmtParts;
        this.elsePart = elsePart;
    }

    @Override
    public String toJavaCode() {
        if (elsePart == null) {
            return condStmtParts.toJavaCode(" else ");
        } else {
            return condStmtParts.toJavaCode(" else ") + " else {\n" +
                Misc.indentLines(elsePart.toJavaCode(), 1) + "\n}";
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}

