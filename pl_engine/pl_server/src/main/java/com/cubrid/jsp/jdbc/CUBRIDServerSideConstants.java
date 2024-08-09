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
package com.cubrid.jsp.jdbc;

public class CUBRIDServerSideConstants {
    /* statement type */
    public static final byte NORMAL = 0,
            GET_BY_OID = 1,
            GET_SCHEMA_INFO = 2,
            GET_AUTOINCREMENT_KEYS = 3;

    /* prepare flags */
    public static final byte PREPARE_INCLUDE_OID = 0x01,
            PREPARE_UPDATABLE = 0x02,
            PREPARE_QUERY_INFO = 0x04,
            PREPARE_HOLDABLE = 0x08,
            PREPARE_XASL_CACHE_PINNED = 0x10,
            PREPARE_CALL = 0x40;

    /* execute flags */
    public static final byte EXEC_FLAG_ASYNC = 0x01,
            EXEC_FLAG_QUERY_ALL = 0x02,
            EXEC_FLAG_QUERY_INFO = 0x04,
            EXEC_FLAG_ONLY_QUERY_PLAN = 0x08,
            EXEC_FLAG_HOLDABLE_RESULT = 0x20,
            EXEC_FLAG_GET_GENERATED_KEYS = 0x40;
    public static final int CURSOR_SET = 0, CURSOR_CUR = 1, CURSOR_END = 2;

    /* oid commands */
    public static final byte DROP_BY_OID = 1,
            IS_INSTANCE = 2,
            GET_READ_LOCK_BY_OID = 3,
            GET_WRITE_LOCK_BY_OID = 4,
            GET_CLASS_NAME_BY_OID = 5;

    /* collection commands */
    public static final byte GET_COLLECTION_VALUE = 1,
            GET_SIZE_OF_COLLECTION = 2,
            DROP_ELEMENT_IN_SET = 3,
            ADD_ELEMENT_TO_SET = 4,
            DROP_ELEMENT_IN_SEQUENCE = 5,
            INSERT_ELEMENT_INTO_SEQUENCE = 6,
            PUT_ELEMENT_ON_SEQUENCE = 7;

    /* end transaction constants */
    public static final byte END_TRAN_COMMIT = 1;
    public static final byte END_TRAN_ROLLBACK = 2;
}
