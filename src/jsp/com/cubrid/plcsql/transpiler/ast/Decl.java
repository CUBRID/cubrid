package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Scope;

public abstract class Decl implements I_Decl {
    public Scope scope;

    public void setScope(Scope scope) {
        this.scope = scope;
    }

    public Scope scope() {
        return scope;
    }
}
