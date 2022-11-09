package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.DateTimeParser;

import java.text.SimpleDateFormat;

import java.time.LocalDate;

public class ExprDate implements I_Expr {

    public final LocalDate date;

    public ExprDate(LocalDate date) {
        this.date = date;
    }

    @Override
    public String toJavaCode() {
        if (date.equals(DateTimeParser.nullDate)) {
            return "new Date(0 - 1900, 0 - 1, 0)";
        } else {
            return String.format("new Date(%d - 1900, %d - 1, %d)",
                date.getYear(),
                date.getMonthValue(),
                date.getDayOfMonth());
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
