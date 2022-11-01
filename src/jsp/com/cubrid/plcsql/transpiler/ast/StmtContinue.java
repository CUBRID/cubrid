package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class StmtContinue implements I_Stmt {

    public final DeclLabel declLabel;

    public StmtContinue(DeclLabel declLabel) {
        this.declLabel = declLabel;
    }

    @Override
    public String toJavaCode() {

        if (declLabel == null) {
            return "continue;";
        } else {
            return String.format("continue $%s_%d;", declLabel.name, declLabel.scope.level);
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
