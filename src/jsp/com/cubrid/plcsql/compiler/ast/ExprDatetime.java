package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.DateTimeParser;

import java.text.SimpleDateFormat;

import java.time.LocalDateTime;

public class ExprDatetime implements I_Expr {

    public final LocalDateTime time;

    public ExprDatetime(LocalDateTime time) {
        this.time = time;
    }

    @Override
    public String toJavaCode() {
        if (time.equals(DateTimeParser.nullDateTime)) {
            return "new Timestamp(0 - 1900, 0 - 1, 0, 0, 0, 0, 0)";
        } else {
            return String.format("new Timestamp(%d - 1900, %d - 1, %d, %d, %d, %d, %d)",
                time.getYear(),
                time.getMonthValue(),
                time.getDayOfMonth(),
                time.getHour(),
                time.getMinute(),
                time.getSecond(),
                time.getNano()
            );
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
