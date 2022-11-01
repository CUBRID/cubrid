package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class StmtForIterLoop implements I_Stmt {

    public final int level;
    public final DeclLabel declLabel;
    public final String iter;
    public final boolean reverse;
    public final I_Expr lowerBound;
    public final I_Expr upperBound;
    public final I_Expr step;
    public final NodeList<I_Stmt> stmts;

    public StmtForIterLoop(int level, DeclLabel declLabel, String iter, boolean reverse,
            I_Expr lowerBound, I_Expr upperBound, I_Expr step, NodeList<I_Stmt> stmts) {

        this.level = level;
        this.declLabel = declLabel;
        this.iter = iter;
        this.reverse = reverse;
        this.lowerBound = lowerBound;
        this.upperBound = upperBound;
        this.step = step;
        this.stmts = stmts;
    }

    @Override
    public String toJavaCode() {

        String labelStr = declLabel == null ? "// no label" : declLabel.toJavaCode();

        return (reverse ? tmplForIterReverse: tmplForIter)
            .replace("%LEVEL%", "" + level)
            .replace("  %OPT-LABEL%", Misc.indentLines(labelStr, 1))
            .replace("%ITER%", iter)
            .replace("%LOWER-BOUND%", lowerBound.toJavaCode())
            .replace("%UPPER-BOUND%", upperBound.toJavaCode())
            .replace("%STEP%", step == null ? "1" : step.toJavaCode())
            .replace("    %STATEMENTS%", Misc.indentLines(stmts.toJavaCode(), 2))
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplForIter = Misc.combineLines(
       "{",
       "  int upper_%LEVEL% = %UPPER-BOUND%;",
       "  int step_%LEVEL% = %STEP%;",
       "  %OPT-LABEL%",
       "  for (int $%ITER%_i%LEVEL% = %LOWER-BOUND%; $%ITER%_i%LEVEL% <= upper_%LEVEL%; $%ITER%_i%LEVEL% += step_%LEVEL%) {",
       "    %STATEMENTS%",
       "  }",
       "}"
    );

    private static final String tmplForIterReverse = Misc.combineLines(
       "{",
       "  int lower_%LEVEL% = %LOWER-BOUND%;",
       "  int step_%LEVEL% = %STEP%;",
       "  %OPT-LABEL%",
       "  for (int $%ITER%_i%LEVEL% = %UPPER-BOUND%; $%ITER%_i%LEVEL% >= lower_%LEVEL%; $%ITER%_i%LEVEL% -= step_%LEVEL%) {",
       "    %STATEMENTS%",
       "  }",
       "}"
    );
}
