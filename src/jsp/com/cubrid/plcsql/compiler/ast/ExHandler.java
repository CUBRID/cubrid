package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

import java.util.List;

public class ExHandler implements AstNode {

    public final List<ExName> exNames;
    public final NodeList<I_Stmt> stmts;

    public ExHandler(List<ExName> exNames, NodeList<I_Stmt> stmts) {
        this.exNames = exNames;
        this.stmts = stmts;
    }

    @Override
    public String toJavaCode() {

        boolean first = true;
        StringBuffer sbuf = new StringBuffer();
        for (ExName ex: exNames) {

            if (first) {
                first = false;
            } else {
                sbuf.append(" | ");
            }

            if ("OTHERS".equals(ex.name)) {
                sbuf.append("Throwable");
            } else if (ex.scope.routine.equals(ex.decl.scope().routine)) {
                sbuf.append("Decl_of_" + ex.decl.scope().block + ".$" + ex.name);
            } else {
                sbuf.append("$" + ex.name);
            }
        }

        return tmpl
            .replace("%EXCEPTIONS%", sbuf.toString())
            .replace("  %STATEMENTS%", Misc.indentLines(stmts.toJavaCode(), 1))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl = Misc.combineLines(
        " catch (%EXCEPTIONS% e) {",
        "  %STATEMENTS%",
        "}"
    );
}
