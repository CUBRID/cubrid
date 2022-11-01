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

    public final NodeList<I_Decl> predefined;
    public final TargetKind targetKind;
    public final boolean autonomousTransaction;
    public final boolean connectionRequired;
    public final DeclRoutine routine;

    public Unit(
            NodeList<I_Decl> predefined,
            TargetKind targetKind,
            boolean autonomousTransaction,
            boolean connectionRequired,
            DeclRoutine routine
        ) {

        this.predefined = predefined;
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
            .replace("  %PREDEFINED%", Misc.indentLines(predefined.toJavaCode("private static ", "\n"), 1))
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
        "",
        "  // ------------------------------",
        "  // PREDEFINED",
        "",
        "  %PREDEFINED%",
        "  private static void $PUT_LINE(Object s) { System.out.println(s); }",
        "  private static Integer $OPEN_CURSOR() { return -1; /* TODO */ }",
        "  private static Integer $LAST_ERROR_POSITION() { return -1; /* TODO */ }",
        "  // end of predefined",
        "",

        "  static class Query {",
        "    final String query;",
        "    ResultSet rs;",
        "    Query(String query) {",
        "      this.query = query;",
        "    }",
        "    void open(Connection conn, Object ... val) throws Exception {",
        "      if (isOpen()) { throw new RuntimeException(\"already open\"); }",
        "      PreparedStatement pstmt = conn.prepareStatement(query);",
        "      for (int i = 0; i < val.length; i++) {",
        "        pstmt.setObject(i + 1, val[i]);",
        "      }",
        "      rs = pstmt.executeQuery();",
        "    }",
        "    void close() throws Exception {",
        "      if (rs != null) {",
        "        Statement stmt = rs.getStatement();",
        "        if (stmt != null) {",
        "          stmt.close();",
        "        }",
        "        rs = null;",
        "      }",
        "    }",
        "    boolean isOpen() throws Exception {",
        "      return (rs != null && !rs.isClosed());",
        "    }",
        "    boolean found() throws Exception {",
        "      if (!isOpen()) { throw new RuntimeException(\"invalid cursor\"); }",
        "      return rs.getRow() > 0;",
        "    }",
        "    boolean notFound() throws Exception {",
        "      if (!isOpen()) { throw new RuntimeException(\"invalid cursor\"); }",
        "      return rs.getRow() == 0;",
        "    }",
        "    int rowCount() throws Exception {",
        "      if (!isOpen()) { throw new RuntimeException(\"invalid cursor\"); }",
        "      return rs.getRow();",
        "    }",
        "  }",

        "  private static Boolean opNot(Boolean l) {",
        "    if (l == null) { return null; }",
        "    return !l;",
        "  }",

        "  private static Boolean opIsNull(Object l) {",
        "    return (l == null);",
        "  }",

        "  private static Integer opNeg(Integer l) {",
        "    if (l == null) { return null; }",
        "    return -l;",
        "  }",

        "  private static Boolean opAnd(Boolean l, Boolean r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l && r;",
        "  }",
        "  private static Boolean opOr(Boolean l, Boolean r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l || r;",
        "  }",
        "  private static Boolean opXor(Boolean l, Boolean r) {",
        "    if (l == null || r == null) { return null; }",
        "    return (l && !r) || (!l && r);",
        "  }",

        "  private static Boolean opEq(Object l, Object r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l.equals(r);",
        "  }",
        "  private static Boolean opNeq(Object l, Object r) {",
        "    if (l == null || r == null) { return null; }",
        "    return !l.equals(r);",
        "  }",

        "  private static Boolean opLe(Integer l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l <= r;",
        "  }",
        "  private static Boolean opGe(Integer l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l >= r;",
        "  }",
        "  private static Boolean opLt(Integer l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l < r;",
        "  }",
        "  private static Boolean opGt(Integer l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l > r;",
        "  }",

        "  private static Boolean opLt(String l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return false; // TODO",
        "  }",
        "  private static Boolean opGt(String l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return false; // TODO",
        "  }",

        "  private static Boolean opLt(Integer l, String r) {",
        "    if (l == null || r == null) { return null; }",
        "    return false; // TODO",
        "  }",
        "  private static Boolean opGt(Integer l, String r) {",
        "    if (l == null || r == null) { return null; }",
        "    return false; // TODO",
        "  }",

        "  private static Boolean opLe(String l, String r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l.compareTo(r) <= 0;",
        "  }",
        "  private static Boolean opGe(String l, String r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l.compareTo(r) >= 0;",
        "  }",
        "  private static Boolean opLt(String l, String r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l.compareTo(r) < 0;",
        "  }",
        "  private static Boolean opGt(String l, String r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l.compareTo(r) > 0;",
        "  }",

        "  private static Boolean opBetween(Integer o, Integer lower, Integer upper) {",
        "    if (o == null || lower == null || upper == null) { return null; }",
        "    return false; // TODO",
        "  }",
        "  private static Boolean opBetween(String o, String lower, String upper) {",
        "    if (o == null || lower == null || upper == null) { return null; }",
        "    return false; // TODO",
        "  }",
        "  private static Boolean opBetween(Integer o, String lower, String upper) {",
        "    if (o == null || lower == null || upper == null) { return null; }",
        "    return false; // TODO",
        "  }",


        "  private static Boolean opIn(Object o, Object... list) {",
        "    if (o == null || list == null) { return null; }",
        "    for (Object p: list) {",
        "      if (Objects.equals(o, p)) {",
        "        return true;",
        "      }",
        "    }",
        "    return false;",
        "  }",

        "  private static Integer opMult(Integer l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l * r;",
        "  }",
        "  private static Integer opDiv(Integer l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l / r;",
        "  }",
        "  private static Integer opMod(Integer l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l % r;",
        "  }",

        "  private static Integer opAdd(Integer l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l + r;",
        "  }",
        "  private static Integer opSubtract(Integer l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l - r;",
        "  }",
        "  private static String opConcat(Object l, Object r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l.toString() + r.toString();",
        "  }",

        "  private static Integer opPower(Integer l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return (int) Math.pow(l, r);",
        "  }",

        "  private static String opAdd(String l, Integer r) {",
        "    if (l == null || r == null) { return null; }",
        "    return l + r; // TODO",
        "  }",

        "  private static Boolean opLike(String s, String pattern, String escape) {",
        "    return false;",    // TODO
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
