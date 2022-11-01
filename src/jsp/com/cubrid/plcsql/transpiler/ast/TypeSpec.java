package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class TypeSpec implements AstNode {

    public final String name;

    public TypeSpec(String name) {
        this.name = name;
    }

    @Override
    public String toJavaCode() {
        return name;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
