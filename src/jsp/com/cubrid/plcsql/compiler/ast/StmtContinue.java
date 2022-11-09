package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

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
