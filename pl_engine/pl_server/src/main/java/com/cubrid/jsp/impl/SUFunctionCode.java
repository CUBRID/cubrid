/*
 *
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

package com.cubrid.jsp.impl;

/**
 * NOTE: This enumeration is ported from UFunctionCode in cubrid-jdbc. Only commands necessary for
 * Server-side JDBC are defined. If there is a command required for Server-side JDBC in the command
 * added in UFunctionCode, it should be added below.
 */
public enum SUFunctionCode {
    END_TRANSACTION(1),
    PREPARE(2),
    EXECUTE(3),
    GET_DB_PARAMETER(4),

    /**
     * This command is not actually supported already It just returns tuple count that you can
     * retrieve from the EXECUTE command
     */
    FETCH(8),
    GET_BY_OID(10),
    PUT_BY_OID(11),

    /**
     * The result of this command can be obtained from DB Server when starting the Java SP Server
     */
    RELATED_TO_OID(17),
    RELATED_TO_COLLECTION(18),
    MAKE_OUT_RS(33),

    GET_GENERATED_KEYS(34),

    /* PL SESSION PARAMETER */
    SET_PL_SESSION_PARAMETER(50),

    LAST_FUNCTION_CODE(-1);

    private int code;

    SUFunctionCode(int code) {
        this.code = code;
    }

    SUFunctionCode(SUFunctionCode code) {
        this.code = code.getCode();
    }

    public int getCode() {
        return this.code;
    }
}
