package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class DeclConst extends Decl implements I_DeclId {

    public final String name;
    public final TypeSpec typeSpec;
    public final I_Expr val;

    public DeclConst(String name, TypeSpec typeSpec, I_Expr val) {
        assert val != null;

        this.name = name;
        this.typeSpec = typeSpec;
        this.val = val;
    }

    public TypeSpec typeSpec() {
        return typeSpec;
    }

    @Override
    public String typeStr() {
        return "constant";
    }

    @Override
    public String toJavaCode() {
        return String.format("%s $%s = %s;", typeSpec.toJavaCode(), name, val.toJavaCode());
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
