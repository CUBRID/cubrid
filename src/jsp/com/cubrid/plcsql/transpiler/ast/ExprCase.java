package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprCase implements I_Expr {

    public final int level;
    public final I_Expr selector;
    public final NodeList<CaseExpr> whenParts;
    public final I_Expr elsePart;

    public ExprCase(int level, I_Expr selector, NodeList<CaseExpr> whenParts, I_Expr elsePart) {
        this.level = level;
        this.selector = selector;
        this.whenParts = whenParts;
        this.elsePart = elsePart;
    }

    @Override
    public String toJavaCode() {

        return tmpl
            .replace("%SELECTOR-VALUE%", selector.toJavaCode())
            .replace("%WHEN-PARTS%", Misc.indentLines(whenParts.toJavaCode(), 2, true))
            .replace("    %ELSE-PART%", Misc.indentLines(elsePart == null ? "null" : elsePart.toJavaCode(), 2))
            .replace("%LEVEL%", "" + level) // level replacement must go last
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "(new Object() { Object invoke(Object selector_%LEVEL%) throws Exception { // simple case expression",
        "  return %WHEN-PARTS%",
        "    %ELSE-PART%;",
        "} }.invoke(%SELECTOR-VALUE%))"
    );
}
