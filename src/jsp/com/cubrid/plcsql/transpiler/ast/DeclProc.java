package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class DeclProc extends DeclRoutine implements I_Decl {

    public DeclProc(
            String name,
            NodeList<I_DeclParam> paramList,
            NodeList<I_Decl> decls,
            Body body
        ) {
        super(name, paramList, null, decls, body);
    }

    @Override
    public String typeStr() {
        return "procedure";
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
