package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;
import com.cubrid.plcsql.transpiler.Scope;

public class StmtBlock implements I_Stmt {

    public final String block;
    public final NodeList<I_Decl> decls;
    public final Body body;

    public StmtBlock(String block, NodeList<I_Decl> decls, Body body) {
        this.block = block;
        this.decls = decls;
        this.body = body;
    }

    @Override
    public String toJavaCode() {

        String strDeclClass = decls == null ? "// no declarations" :
            tmplDeclClass
                .replace("%BLOCK%", block)
                .replace("  %DECLARATIONS%", Misc.indentLines(decls.toJavaCode(), 1))
                ;

        return tmplBlock
            .replace("  %DECL-CLASS%", Misc.indentLines(strDeclClass, 1))
            .replace("  %BODY%", Misc.indentLines(body.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplBlock = Misc.combineLines(
        "{",
        "",
        "  %DECL-CLASS%",
        "",
        "  %BODY%",
        "}"
    );

    private static final String tmplDeclClass = Misc.combineLines(
        "class Decl_of_%BLOCK% {",
        "  Decl_of_%BLOCK%() throws Exception {};",
        "  %DECLARATIONS%",
        "}",
        "Decl_of_%BLOCK% %BLOCK% = new Decl_of_%BLOCK%();"
    );
}
