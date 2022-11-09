package com.cubrid.plcsql.compiler.ast;

import java.time.temporal.TemporalAccessor;

public class ExprTimestampLTZ extends ExprZonedDateTime {

    public ExprTimestampLTZ(TemporalAccessor time) {
        super(time);
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
