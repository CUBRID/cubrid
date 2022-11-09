package com.cubrid.plcsql.compiler.ast;

import java.time.temporal.TemporalAccessor;

public class ExprDatetimeLTZ extends ExprZonedDateTime {

    public ExprDatetimeLTZ(TemporalAccessor time) {
        super(time);
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
