package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtBreak implements I_Stmt {

    public final DeclLabel declLabel;

    public StmtBreak(DeclLabel declLabel) {
        this.declLabel = declLabel;
    }

    @Override
    public String toJavaCode() {

        if (declLabel == null) {
            return "break;";
        } else {
            return String.format("break $%s_%d;", declLabel.name, declLabel.scope.level);
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
