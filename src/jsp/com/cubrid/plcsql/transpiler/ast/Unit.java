package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;
import com.cubrid.plcsql.transpiler.Scope;
import com.cubrid.plcsql.transpiler.ParseTreeConverter;

import java.sql.*;

public class Unit implements AstNode {

    public enum TargetKind {
        FUNCTION,
        PROCEDURE
    };

    public final TargetKind targetKind;
    public final boolean autonomousTransaction;
    public final boolean connectionRequired;
    public final DeclRoutine routine;

    public Unit(
            TargetKind targetKind,
            boolean autonomousTransaction,
            boolean connectionRequired,
            DeclRoutine routine
        ) {

        this.targetKind = targetKind;
        this.autonomousTransaction = autonomousTransaction;
        this.connectionRequired = connectionRequired;
        this.routine = routine;
    }

    @Override
    public String toJavaCode() {

        String strGetConn;
        if (connectionRequired) {
            strGetConn = tmplGetConn
                .replace("%AUTONOMOUS-TRANSACTION%", autonomousTransaction ? "?autonomous_transaction=true" : "");
        } else {
            strGetConn = "// connection not required";
        }

        String strDecls = routine.decls == null ? "// no declarations" :
            tmplDeclClass.replace("%BLOCK%", routine.name.toLowerCase())
                         .replace("  %DECLARATIONS%", Misc.indentLines(routine.decls.toJavaCode(), 1));;
        String strParams = routine.paramList == null ? "// no parameters" : routine.paramList.toJavaCode(",\n");

        return tmplUnit
            .replace("%IMPORTS%", ParseTreeConverter.getImportString())
            .replace("%CLASS-NAME%", getClassName())
            .replace("%RETURN-TYPE%", routine.retType == null ? "void" : routine.retType.toJavaCode())
            .replace("%METHOD-NAME%", routine.name)
            .replace("      %PARAMETERS%", Misc.indentLines(strParams, 3))
            .replace("    %GET-CONNECTION%", Misc.indentLines(strGetConn, 2))
            .replace("    %DECL-CLASS%", Misc.indentLines(strDecls, 2))
            .replace("    %BODY%", Misc.indentLines(routine.body.toJavaCode(), 2))
            ;
    }

    public String getClassName() {

        if (className == null) {
            String kindStr = (targetKind == TargetKind.FUNCTION) ? "Func" :
                            (targetKind == TargetKind.PROCEDURE) ? "Proc" : null;
            assert kindStr != null;

            className = String.format("%s_%s", kindStr, routine.name);
        }

        return className;
    }

    // ------------------------------------------
    // Private
    // ------------------------------------------

    private static final String tmplUnit = Misc.combineLines(
        "%IMPORTS%",
        "import static com.cubrid.plcsql.lib.sp.SpLib.*;",
        "",
        "public class %CLASS-NAME% {",
        "",
        "  public static %RETURN-TYPE% $%METHOD-NAME%(",
        "      %PARAMETERS%",
        "    ) throws Exception {",
        "",
        //"    Long[] sql_rowcount = new Long[] { -1L };",
        "    Integer[] sql_rowcount = new Integer[] { -1 };",
        "    %GET-CONNECTION%",
        "",
        "    %DECL-CLASS%",
        "",
        "    %BODY%",
        "  }",
        "}"
    );

    private static final String tmplGetConn = Misc.combineLines(
        "Connection conn = DriverManager.getConnection" +
            "(\"jdbc:default:connection:%AUTONOMOUS-TRANSACTION%\");"
    );

    private static final String tmplDeclClass = Misc.combineLines(
        "class Decl_of_%BLOCK% {",
        "  Decl_of_%BLOCK%() throws Exception {};",
        "  %DECLARATIONS%",
        "}",
        "Decl_of_%BLOCK% %BLOCK% = new Decl_of_%BLOCK%();"
    );

    private String className;
}
