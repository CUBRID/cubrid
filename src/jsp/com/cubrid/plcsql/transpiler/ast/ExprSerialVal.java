package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

public class ExprSerialVal implements I_Expr {

    public enum SerialVal {
        CURR_VAL,
        NEXT_VAL,
    }

    public final int level;
    public final String name;
    public final SerialVal mode;  // CURR_VAL or NEXT_VAL

    public ExprSerialVal(int level, String name, SerialVal mode) {
        this.level = level;
        this.name = name;
        this.mode = mode;
    }

    @Override
    public String toJavaCode() {

        return tmplSerialVal
            .replace("%SERIAL-NAME%", name)
            .replace("%SERIAL-VAL%", (mode == SerialVal.CURR_VAL) ? "CURRENT_VALUE" : "NEXT_VALUE")
            .replace("%LEVEL%", "" + level)
            ;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplSerialVal = Misc.combineLines(
        "(new Object() {",
        "  int getSerialVal() throws Exception {",
        "    int ret_%LEVEL%;",
        "    String dynSql_%LEVEL% = \"select %SERIAL-NAME%.%SERIAL-VAL%\";",
        "    PreparedStatement stmt_%LEVEL% = conn.prepareStatement(dynSql_%LEVEL%);",
        "    ResultSet r%LEVEL% = stmt_%LEVEL%.executeQuery();",
        "    if (r%LEVEL%.next()) {",
        "      ret_%LEVEL% = r%LEVEL%.getInt(1);",
        "    } else {",
        "      assert false; // serial value must be present",
        "      ret_%LEVEL% = -1;",
        "    }",
        "    stmt_%LEVEL%.close();",
        "    return ret_%LEVEL%;",
        "  }",
        "}.getSerialVal())"
    );
}
