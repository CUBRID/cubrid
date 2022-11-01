package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Scope;
import com.cubrid.plcsql.transpiler.Misc;

public class ExName implements AstNode {

    public final String name;
    public final Scope scope;
    public final DeclException decl;

    public ExName(String name, Scope scope, DeclException decl) {
        this.name = name;
        this.scope = scope;
        this.decl = decl;
    }

    @Override
    public String toJavaCode() {
        assert false;   // depends on the context in which this node is located and must not be called directly
        return null;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
