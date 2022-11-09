package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class DeclFunc extends DeclRoutine implements I_Decl {

    public DeclFunc(
            String name,
            NodeList<I_DeclParam> paramList,
            TypeSpec retType,
            NodeList<I_Decl> decls,
            Body body
        ) {
        super(name, paramList, retType, decls, body);
    }

    @Override
    public String typeStr() {
        return "function";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
