package com.cubrid.plcsql.transpiler.ast;

import java.time.temporal.TemporalAccessor;

public class ExprDatetimeLTZ extends ExprZonedDateTime {

    public ExprDatetimeLTZ(TemporalAccessor time) {
        super(time);
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
