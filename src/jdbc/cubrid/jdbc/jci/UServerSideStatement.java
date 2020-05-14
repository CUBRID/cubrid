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

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.io.IOException;

import cubrid.sql.CUBRIDOID;

public class UServerSideStatement extends UStatement {

	public UServerSideStatement(UConnection relatedC, CUBRIDOID oid, String[] attributeName, UInputBuffer inBuffer)
			throws UJciException {
		super(relatedC, oid, attributeName, inBuffer);
		// TODO Auto-generated constructor stub
	}

	public UServerSideStatement(UConnection u_con, int srv_handle) throws UJciException, IOException {
		super(u_con, srv_handle);
		// TODO Auto-generated constructor stub
	}

	public UServerSideStatement(UConnection relatedC, String cName, String attributePattern, int type,
			UInputBuffer inBuffer) throws UJciException {
		super(relatedC, cName, attributePattern, type, inBuffer);
		// TODO Auto-generated constructor stub
	}

	public UServerSideStatement(UConnection relatedC, UInputBuffer inBuffer, boolean assign_only, String sql,
			byte _prepare_flag) throws UJciException {
		super(relatedC, inBuffer, assign_only, sql, _prepare_flag);
		// TODO Auto-generated constructor stub
	}

	public UServerSideStatement(UStatement u_stmt) {
		super(u_stmt);
		// TODO Auto-generated constructor stub
	}
	
	/*
	@Override
	public void reset(byte flag) throws UJciException {
		
	}
	*/

	@Override
	public synchronized void close(boolean close_srv_handle) {
		// TODO Auto-generated method stub
		super.close(close_srv_handle);
	}
}
