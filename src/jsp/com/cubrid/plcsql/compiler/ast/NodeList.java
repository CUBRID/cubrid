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

import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import java.util.ArrayList;
import java.util.List;

public class NodeList<N extends AstNode> extends AstNode {

    public final List<N> nodes = new ArrayList<>();

    public NodeList() {
        super(null);
    }

    public NodeList<N> addNode(N p) {
        assert p != null;
        nodes.add(p);
        return this;
    }

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitNodeList(this);
    }

    @Override
    public String toJavaCode() {
        return toJavaCode(null, "\n");
    }

    public String toJavaCode(String delim) {
        return toJavaCode(null, delim);
    }

    public String toJavaCode(String prefix, String delim) {

        StringBuffer sbuf = new StringBuffer();

        boolean first = true;
        for (N p : nodes) {

            if (delim != null) {
                if (first) {
                    first = false;
                } else {
                    sbuf.append(delim);
                }
            }

            if (prefix != null) {
                sbuf.append(prefix);
            }

            sbuf.append(p.toJavaCode());
        }

        return sbuf.toString();
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
