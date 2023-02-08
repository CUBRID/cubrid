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

package com.cubrid.plcsql.compiler;

import com.cubrid.plcsql.compiler.antlrgen.PcsLexer;
import org.antlr.v4.runtime.CharStream;
import org.antlr.v4.runtime.Token;

public class PcsLexerEx extends PcsLexer {
    private boolean collect = true;
    private boolean putSpace = false;
    private StringBuffer sbuf = new StringBuffer();

    public PcsLexerEx(CharStream input) {
        super(input);
    }

    @Override
    public Token emit() {
        Token ret = super.emit();

        // collect token texts until IS or AS is seen
        if (collect) {
            switch (ret.getType()) {
                case IS:
                case AS:
                    collect = false;
                    break;
                case SPACES:
                    if (putSpace) {
                        sbuf.append(' ');
                        putSpace = false;
                    }
                    break;
                case SINGLE_LINE_COMMENT:
                case SINGLE_LINE_COMMENT2:
                case MULTI_LINE_COMMENT:
                    break;
                default:
                    sbuf.append(ret.getText().toUpperCase());
                    putSpace = true;
            }
        }

        return ret;
    }

    public String getCreateSqlTemplate() {

        String s = sbuf.toString().trim();
        if (s.length() > 0) {
            return s + " AS LANGUAGE JAVA NAME '%s';";
        } else {
            return null;
        }
    }
}
