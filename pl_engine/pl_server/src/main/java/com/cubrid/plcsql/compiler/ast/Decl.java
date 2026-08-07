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

import com.cubrid.jsp.data.CompileResponse;
import com.cubrid.plcsql.compiler.Scope;
import org.antlr.v4.runtime.ParserRuleContext;

/*
Decl
   - DeclLabel
   - DeclException
   - DeclPackage
   - DeclRoutine
       - DeclFunc
       - DeclProc
   - DeclId
       - DeclDynamicRecord
       - DeclForIter
       - DeclIdTypeDeclared
           - DeclParam
               - DeclParamIn
               - DeclParamOut
           - DeclConst
           - DeclVar
       - DeclCursor

*/

public abstract class Decl extends AstNode {

    public final String name;
    public final String comment;
    public Scope scope;
    // some kinds of declarations (currently, procedure, function, and cursor) can be declared
    // without a body (forward decl)
    // and other declarations can give them a body (body decl).
    // the following field links the former to the latter in such cases.
    public Decl bodyDecl;
    // Java code generation for this node can have been done when it is the implementation of
    // a declaration in a package specification
    public boolean codeGenDone;

    public abstract String kind();

    public Decl(ParserRuleContext ctx, String name, String comment) {
        super(ctx);
        this.name = name;
        this.comment = comment;
    }

    public void setScope(Scope scope) {
        this.scope = scope;
    }

    public void setBodyDecl(Decl bodyDecl) {
        this.bodyDecl = bodyDecl;
    }

    // whether this decl provides a body to the other decl or not
    public boolean givesBodyOf(Decl other) {
        // by default false
        // DeclCursor, DeclFunc, DeclProc will override this default
        return false;
    }

    // whether this decl has a body or a body decl or not
    public boolean lackOfBody() {
        // by default false
        // DeclCursor, DeclRoutine will override this default
        return false;
    }

    // add package item information to the compile response message
    public void addAsPkgItem(CompileResponse resp) {
        // by default, unreachable
        // declarations which can be a package item will properly override this method
        assert false;
    }
}
