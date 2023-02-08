/*
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

import com.cubrid.plcsql.compiler.SemanticError;
import com.cubrid.plcsql.compiler.visitor.AstNodeVisitor;

import com.cubrid.plcsql.compiler.Misc;

public class ExprBetween implements Expr {

    @Override
    public <R> R accept(AstNodeVisitor<R> visitor) {
        return visitor.visitExprBetween(this);
    }

    public final Expr target;
    public final Expr lowerBound;
    public final Expr upperBound;

    public ExprBetween(Expr target, Expr lowerBound, Expr upperBound) {
        this.target = target;
        this.lowerBound = lowerBound;
        this.upperBound = upperBound;
    }

    @Override
    public String toJavaCode() {
        return tmpl.replace("  %'TARGET'%", Misc.indentLines(target.toJavaCode(), 1))
                .replace("  %'LOWER-BOUND'%", Misc.indentLines(lowerBound.toJavaCode(), 1))
                .replace("  %'UPPER-BOUND'%", Misc.indentLines(upperBound.toJavaCode(), 1));
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmpl =
            Misc.combineLines(
                    "opBetween(", "  %'TARGET'%,", "  %'LOWER-BOUND'%,", "  %'UPPER-BOUND'%", ")");
}
