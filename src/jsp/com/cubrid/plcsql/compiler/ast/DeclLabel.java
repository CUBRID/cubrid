package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class DeclLabel extends Decl implements I_Decl {

    public final String name;

    public DeclLabel(String name) {
        this.name = name;
    }

    @Override
    public String typeStr() {
        return "label";
    }

    @Override
    public String toJavaCode() {
        return String.format("$%s_%d:", name, scope.level);
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
