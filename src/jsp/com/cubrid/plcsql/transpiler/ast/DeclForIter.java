package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class DeclForIter extends Decl implements I_DeclId {

    public final String name;
    public final TypeSpec typeSpec;

    public DeclForIter(String name) {
        this.name = name;
        typeSpec = new TypeSpec("Integer");
    }

    public TypeSpec typeSpec() {
        return typeSpec;
    }

    @Override
    public String typeStr() {
        return "for-loop-iterator";
    }

    @Override
    public String toJavaCode() {
        assert false;   // must not be called directly
        return null;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
