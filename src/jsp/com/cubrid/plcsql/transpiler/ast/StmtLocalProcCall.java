package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;
import com.cubrid.plcsql.transpiler.Scope;

public class StmtLocalProcCall implements I_Stmt {

    public final String name;
    public final NodeList<I_Expr> args;
    public final Scope scope;
    public final DeclProc decl;

    public StmtLocalProcCall(String name, NodeList<I_Expr> args, Scope scope, DeclProc decl) {
        this.name = name;
        this.args = args;
        this.scope = scope;
        this.decl = decl;
    }

    @Override
    public String toJavaCode() {

        String block = scope.routine.equals(decl.scope().routine) ? decl.scope().block + "." : "";

        if (args == null || args.nodes.size() == 0) {
            return block + "$" + name + "();";
        } else {
            return block + "$" + name + "(\n" +
                Misc.indentLines(decl.argsToJavaCode(args), 1) +
                "\n);"
                ;
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
