/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
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

package com.cubrid.jsp.value;

import java.sql.SQLException;

import com.cubrid.jsp.Server;
import com.cubrid.jsp.exception.TypeMismatchException;

import cubrid.sql.CUBRIDOID;

public class OidValue extends Value {
	private CUBRIDOID oid;

	public OidValue(CUBRIDOID oid) {
		super();
		this.oid = oid;
	}

	public OidValue(CUBRIDOID oid, int mode, int dbType) {
		super(mode);
		this.oid = oid;
		this.dbType = dbType;
	}

	public CUBRIDOID toOid() throws TypeMismatchException {
		return oid;
	}

	public CUBRIDOID[] toOidArray() throws TypeMismatchException {
		return new CUBRIDOID[] { oid };
	}

	public String toString() {
		try {
			return oid.getOidString();
		} catch (SQLException e) {
			Server.log(e);
		}
		return null;
	}

	public String[] toStringArray() throws TypeMismatchException {
		return new String[] { toString() };
	}

	public Object toObject() throws TypeMismatchException {
		return toOid();
	}

	public Object[] toObjectArray() throws TypeMismatchException {
		return new Object[] { toObject() };
	}
}
