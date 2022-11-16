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
import java.sql.*;

public class Unit implements AstNode {

    public enum TargetKind {
        FUNCTION,
        PROCEDURE
    };

    public final TargetKind targetKind;
    public final boolean autonomousTransaction;
    public final boolean connectionRequired;
    public final String importsStr;
    public final DeclRoutine routine;

    public Unit(
            TargetKind targetKind,
            boolean autonomousTransaction,
            boolean connectionRequired,
            String importsStr,
            DeclRoutine routine) {

        this.targetKind = targetKind;
        this.autonomousTransaction = autonomousTransaction;
        this.connectionRequired = connectionRequired;
        this.importsStr = importsStr;
        this.routine = routine;
    }

    @Override
    public String toJavaCode() {

        String strGetConn;
        if (connectionRequired) {
            strGetConn =
                    tmplGetConn.replace(
                            "%AUTONOMOUS-TRANSACTION%",
                            autonomousTransaction ? "?autonomous_transaction=true" : "");
        } else {
            strGetConn = "// connection not required";
        }

        String strDecls =
                routine.decls == null
                        ? "// no declarations"
                        : tmplDeclClass
                                .replace("%BLOCK%", routine.name.toLowerCase())
                                .replace(
                                        "  %DECLARATIONS%",
                                        Misc.indentLines(routine.decls.toJavaCode(), 1));
        ;
        String strParams =
                routine.paramList == null
                        ? "// no parameters"
                        : routine.paramList.toJavaCode(",\n");

        return tmplUnit.replace("%IMPORTS%", importsStr)
                .replace("%CLASS-NAME%", getClassName())
                .replace(
                        "%RETURN-TYPE%",
                        routine.retType == null ? "void" : routine.retType.toJavaCode())
                .replace("%METHOD-NAME%", routine.name)
                .replace("      %PARAMETERS%", Misc.indentLines(strParams, 3))
                .replace("    %GET-CONNECTION%", Misc.indentLines(strGetConn, 2))
                .replace("    %DECL-CLASS%", Misc.indentLines(strDecls, 2))
                .replace("    %BODY%", Misc.indentLines(routine.body.toJavaCode(), 2));
    }

    public String getClassName() {

        if (className == null) {
            String kindStr =
                    (targetKind == TargetKind.FUNCTION)
                            ? "Func"
                            : (targetKind == TargetKind.PROCEDURE) ? "Proc" : null;
            assert kindStr != null;

            className = String.format("%s_%s", kindStr, routine.name);
        }

        return className;
    }

    // ------------------------------------------
    // Private
    // ------------------------------------------

    private static final String tmplUnit =
            Misc.combineLines(
                    "%IMPORTS%",
                    "import static com.cubrid.plcsql.lib.sp.SpLib.*;",
                    "",
                    "public class %CLASS-NAME% {",
                    "",
                    "  public static %RETURN-TYPE% $%METHOD-NAME%(",
                    "      %PARAMETERS%",
                    "    ) throws Exception {",
                    "",
                    // "    Long[] sql_rowcount = new Long[] { -1L };",
                    "    Integer[] sql_rowcount = new Integer[] { -1 };",
                    "    %GET-CONNECTION%",
                    "",
                    "    %DECL-CLASS%",
                    "",
                    "    %BODY%",
                    "  }",
                    "}");

    private static final String tmplGetConn =
            Misc.combineLines(
                    "Connection conn = DriverManager.getConnection"
                            + "(\"jdbc:default:connection:%AUTONOMOUS-TRANSACTION%\");");

    private static final String tmplDeclClass =
            Misc.combineLines(
                    "class Decl_of_%BLOCK% {",
                    "  Decl_of_%BLOCK%() throws Exception {};",
                    "  %DECLARATIONS%",
                    "}",
                    "Decl_of_%BLOCK% %BLOCK% = new Decl_of_%BLOCK%();");

    private String className;
}
