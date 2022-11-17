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

import com.cubrid.plcsql.compiler.Misc;

public class DeclRoutine extends Decl {

    public final String name;
    public final NodeList<I_DeclParam> paramList;
    public final TypeSpec retType;
    public NodeList<I_Decl> decls;
    public Body body;

    public DeclRoutine(
            String name,
            NodeList<I_DeclParam> paramList,
            TypeSpec retType,
            NodeList<I_Decl> decls,
            Body body) {

        this.name = name;
        this.paramList = paramList;
        this.retType = retType;
        this.decls = decls;
        this.body = body;
    }

    @Override
    public String typeStr() {
        return "function";
    }

    @Override
    public String toJavaCode() {

        String strDeclClass =
                decls == null
                        ? "// no declarations"
                        : tmplDeclClass
                                .replace("%BLOCK%", name.toLowerCase())
                                .replace(
                                        "  %DECLARATIONS%",
                                        Misc.indentLines(decls.toJavaCode(), 1));
        String strParams = paramList == null ? "// no parameters" : paramList.toJavaCode(",\n");

        return tmplFuncBody
                .replace("%RETURN-TYPE%", retType == null ? "void" : retType.toJavaCode())
                .replace("    %PARAMETERS%", Misc.indentLines(strParams, 2))
                .replace("  %DECL-CLASS%", Misc.indentLines(strDeclClass, 1))
                .replace("  %BODY%", Misc.indentLines(body.toJavaCode(), 1))
                .replace("%METHOD-NAME%", name);
    }

    // --------------------------------------------------
    // Package
    // --------------------------------------------------

    public String argsToJavaCode(NodeList<I_Expr> args) {

        assert args != null;
        assert args.nodes.size() == paramList.nodes.size()
                : "formal and actual parameter of routine " + name + "does not match";

        StringBuffer sbuf = new StringBuffer();

        int i = 0;
        for (I_Expr a : args.nodes) {

            if (i > 0) {
                sbuf.append(",\n");
            }

            I_DeclParam dp = paramList.nodes.get(i);
            if (dp instanceof DeclParamOut) {
                sbuf.append(((ExprId) a).toJavaCodeForOutParam());
            } else {
                sbuf.append(a.toJavaCode());
            }

            i++;
        }

        return sbuf.toString();
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplFuncBody =
            Misc.combineLines(
                    "%RETURN-TYPE% $%METHOD-NAME%(",
                    "    %PARAMETERS%",
                    "  ) throws Exception {",
                    "",
                    "  %DECL-CLASS%",
                    "",
                    "  %BODY%",
                    "}");

    private static final String tmplDeclClass =
            Misc.combineLines(
                    "class Decl_of_%BLOCK% {",
                    "  Decl_of_%BLOCK%() throws Exception {};",
                    "  %DECLARATIONS%",
                    "}",
                    "Decl_of_%BLOCK% %BLOCK% = new Decl_of_%BLOCK%();");
}
