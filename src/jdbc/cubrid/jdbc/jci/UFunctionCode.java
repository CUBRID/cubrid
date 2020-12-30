/*
 * Copyright (C) 2008 Search Solution Corporation. 
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

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

/**
 * CAS Protocol Function code
 * 
 * since 1.0
 */

public enum UFunctionCode {
	/* since 1.0 */
	
	END_TRANSACTION (1),
	PREPARE (2),
	EXECUTE (3),
	GET_DB_PARAMETER (4),
	SET_DB_PARAMETER (5),
	CLOSE_USTATEMENT (6),
	CURSOR (7),
	FETCH (8),
	GET_SCHEMA_INFO (9),
	GET_BY_OID (10),
	PUT_BY_OID (11),
	GET_DB_VERSION (15),
	GET_CLASS_NUMBER_OBJECTS (16),
	RELATED_TO_OID (17),
	RELATED_TO_COLLECTION (18),
	/* since 2.0 */
	NEXT_RESULT (19),
	EXECUTE_BATCH_STATEMENT (20),
	EXECUTE_BATCH_PREPAREDSTATEMENT (21),
	CURSOR_UPDATE (22),
	GET_QUERY_INFO (24),

	/* since 3.0 */
	SAVEPOINT (26),
	PARAMETER_INFO (27),
	XA_PREPARE (28),
	XA_RECOVER (29),
	XA_END_TRAN (30),

	CON_CLOSE (31),
	CHECK_CAS (32),

	MAKE_OUT_RS (33),

	GET_GENERATED_KEYS (34),

	NEW_LOB (35),
	WRITE_LOB (36),
	READ_LOB (37),

	END_SESSION (38),
	PREPARE_AND_EXECUTE (41),
	CURSOR_CLOSE_FOR_PROTOCOL_V2 (41),
	CURSOR_CLOSE (42),
	GET_SHARD_INFO (43),
	SET_CAS_CHANGE_MODE (44),
	LAST_FUNCTION_CODE (GET_SHARD_INFO);
	
	private byte code;
	UFunctionCode(byte code) {
		this.code = code;
	}
	
	UFunctionCode(int code) {
		// FIX ME: possibly overflow 
		this.code = (byte) code;
	}
	
	UFunctionCode(UFunctionCode code) {
		this.code = code.getCode();
	}
	
	public byte getCode() {
		return this.code;
	}
}
