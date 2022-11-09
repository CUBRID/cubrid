package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Scope;

public abstract class Decl implements I_Decl {
    public Scope scope;

    public void setScope(Scope scope) {
        this.scope = scope;
    }

    public Scope scope() {
        return scope;
    }
}
