package com.cubrid.plcsql.transpiler.ast;

import java.time.temporal.TemporalAccessor;

public class ExprTimestampLTZ extends ExprZonedDateTime {

    public ExprTimestampLTZ(TemporalAccessor time) {
        super(time);
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
