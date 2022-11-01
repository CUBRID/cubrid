package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class DeclParamIn extends Decl implements I_DeclParam {

    public final String name;
    public final TypeSpec typeSpec;

    public DeclParamIn(String name, TypeSpec typeSpec) {
        this.name = name;
        this.typeSpec = typeSpec;
    }

    public TypeSpec typeSpec() {
        return typeSpec;
    }

    @Override
    public String typeStr() {
        return "in-parameter";
    }

    @Override
    public String toJavaCode() {
        return String.format("%s $%s", typeSpec.toJavaCode(), name);
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
