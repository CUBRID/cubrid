package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class DeclProc extends DeclRoutine {

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
