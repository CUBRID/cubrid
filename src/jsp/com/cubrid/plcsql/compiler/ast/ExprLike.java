package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class ExprLike implements I_Expr {

    public final I_Expr target;
    public final I_Expr pattern;
    public final I_Expr escape;

    public ExprLike(I_Expr target, I_Expr pattern, I_Expr escape) {
        this.target = target;
        this.pattern = pattern;
        this.escape = escape;
    }

    @Override
    public String toJavaCode() {
        return tmpl
            .replace("  %TARGET%", Misc.indentLines(target.toJavaCode(), 1))
            .replace("  %PATTERN%", Misc.indentLines(pattern.toJavaCode(), 1))
            .replace("  %ESCAPE%", Misc.indentLines(escape == null ? "null" : escape.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        "opLike(",
        "  %TARGET%,",
        "  %PATTERN%,",
        "  %ESCAPE%",
        ")"
    );

}
