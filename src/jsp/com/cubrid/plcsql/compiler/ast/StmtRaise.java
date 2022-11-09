package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtRaise implements I_Stmt {

    public final ExName exName;

    public StmtRaise(ExName exName) {
        this.exName = exName;
    }

    @Override
    public String toJavaCode() {
        if (exName == null) {
            return "throw new Exception();";
        } else if (exName.scope.routine.equals(exName.decl.scope().routine)) {
            return String.format("throw %s.new $%s();", exName.decl.scope().block, exName.name);
        } else {
            return String.format("throw new $%s();", exName.name);
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
