package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class DeclException extends Decl implements I_Decl {

    public final String name;

    public DeclException(String name) {
        this.name = name;
    }

    @Override
    public String typeStr() {
        return "exception";
    }

    @Override
    public String toJavaCode() {
        return "class $" + name + " extends RuntimeException {}";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
