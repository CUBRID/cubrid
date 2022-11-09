package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.DateTimeParser;

import java.text.SimpleDateFormat;

import java.time.temporal.TemporalAccessor;
import java.time.ZonedDateTime;
import java.time.LocalDateTime;

public class ExprZonedDateTime implements I_Expr {

    public final TemporalAccessor time;

    public ExprZonedDateTime(TemporalAccessor time) {
        this.time = time;
    }

    @Override
    public String toJavaCode() {

        String offsetStr;
        LocalDateTime localPart;
        if (time instanceof ZonedDateTime) {
            offsetStr = '"' + ((ZonedDateTime) time).getOffset().getId() + '"';
            localPart = ((ZonedDateTime) time).toLocalDateTime();
        } else {
            assert time instanceof LocalDateTime;
            offsetStr = "null";
            localPart = (LocalDateTime) time;
        }

        if (localPart.equals(DateTimeParser.nullDateTime)) {
            return String.format("new ZonedTimestamp(%s, 0 - 1900, 0 - 1, 0, 0, 0, 0, 0)", offsetStr);
        } else {
            return String.format("new ZonedTimestamp(%s, %d - 1900, %d - 1, %d, %d, %d, %d, %d)",
                offsetStr,
                localPart.getYear(),
                localPart.getMonthValue(),
                localPart.getDayOfMonth(),
                localPart.getHour(),
                localPart.getMinute(),
                localPart.getSecond(),
                localPart.getNano()
            );
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
