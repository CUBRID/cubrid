package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class StmtCase implements I_Stmt {

    public final int level;
    public final I_Expr selector;
    public final NodeList<CaseStmt> whenParts;
    public final NodeList<I_Stmt> elsePart;

    public StmtCase(int level, I_Expr selector, NodeList<CaseStmt> whenParts, NodeList<I_Stmt> elsePart) {
        this.level = level;
        this.selector = selector;
        this.whenParts = whenParts;
        this.elsePart = elsePart;
    }

    @Override
    public String toJavaCode() {

        if (elsePart == null) {
            return tmplStmtCaseNoElsePart
                .replace("%SELECTOR-VALUE%", selector.toJavaCode())
                .replace("  %WHEN-PARTS%", Misc.indentLines(whenParts.toJavaCode(" else "), 1))
                .replace("%LEVEL%", "" + level) // level replacement must go last
                ;
        } else {
            return tmplStmtCase
                .replace("%SELECTOR-VALUE%", selector.toJavaCode())
                .replace("  %WHEN-PARTS%", Misc.indentLines(whenParts.toJavaCode(" else "), 1))
                .replace("    %ELSE-PART%", Misc.indentLines(elsePart.toJavaCode(), 2))
                .replace("%LEVEL%", "" + level) // level replacement must go last
                ;
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplStmtCaseNoElsePart = Misc.combineLines(
        "{",
        "  Object selector_%LEVEL% = %SELECTOR-VALUE%;",
        "  %WHEN-PARTS%",
        "}"
    );

    private static final String tmplStmtCase = Misc.combineLines(
        "{",
        "  Object selector_%LEVEL% = %SELECTOR-VALUE%;",
        "  %WHEN-PARTS% else {",
        "    %ELSE-PART%",
        "  }",
        "}"
    );
}
