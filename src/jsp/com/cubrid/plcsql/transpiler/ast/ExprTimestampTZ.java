package com.cubrid.plcsql.transpiler.ast;

import java.time.temporal.TemporalAccessor;

public class ExprTimestampTZ extends ExprZonedDateTime {

    public ExprTimestampTZ(TemporalAccessor time) {
        super(time);
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
