/*
 * Copyright (C) 2008 Search Solution Corporation.
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

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
