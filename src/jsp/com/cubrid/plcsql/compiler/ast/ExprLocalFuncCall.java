package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.Scope;

public class ExprLocalFuncCall implements I_Expr {

    public final String name;
    public final NodeList<I_Expr> args;
    public final Scope scope;
    public final DeclFunc decl;

    public ExprLocalFuncCall(String name, NodeList<I_Expr> args, Scope scope, DeclFunc decl) {
        this.name = name;
        this.args = args;
        this.scope = scope;
        this.decl = decl;
    }

    @Override
    public String toJavaCode() {

        String block = scope.routine.equals(decl.scope().routine) ? decl.scope().block + "." : "";

        if (args == null || args.nodes.size() == 0) {
            return block + "$" + name + "()";
        } else {
            return block + "$" + name + "(\n" +
                Misc.indentLines(decl.argsToJavaCode(args), 1) +
                "\n)"
                ;
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
