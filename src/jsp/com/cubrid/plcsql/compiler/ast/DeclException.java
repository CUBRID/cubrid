package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class DeclException extends Decl {

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
