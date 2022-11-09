package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class DeclVar extends Decl implements I_DeclId {

    public final String name;
    public final TypeSpec typeSpec;
    public final I_Expr val;

    public DeclVar(String name, TypeSpec typeSpec, I_Expr val) {
        this.name = name;
        this.typeSpec = typeSpec;
        this.val = val;
    }

    public TypeSpec typeSpec() {
        return typeSpec;
    }

    @Override
    public String typeStr() {
        return "variable";
    }

    @Override
    public String toJavaCode() {
        String ty = typeSpec.toJavaCode();
        if (val == null) {
            return String.format("%s[] $%s = new %s[] { null };", ty, name, ty);
        } else {
            return String.format("%s[] $%s = new %s[] { %s };", ty, name, ty, val.toJavaCode());
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
