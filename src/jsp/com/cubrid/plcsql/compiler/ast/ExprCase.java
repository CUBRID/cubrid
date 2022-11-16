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

        return tmpl.replace("%SELECTOR-VALUE%", selector.toJavaCode())
                .replace("%WHEN-PARTS%", Misc.indentLines(whenParts.toJavaCode(), 2, true))
                .replace(
                        "    %ELSE-PART%",
                        Misc.indentLines(elsePart == null ? "null" : elsePart.toJavaCode(), 2))
                .replace("%LEVEL%", "" + level) // level replacement must go last
        ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl =
            Misc.combineLines(
                    "(new Object() { Object invoke(Object selector_%LEVEL%) throws Exception { // simple case expression",
                    "  return %WHEN-PARTS%", "    %ELSE-PART%;", "} }.invoke(%SELECTOR-VALUE%))");
}
