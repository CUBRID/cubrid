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

package cubrid.jdbc.driver;

import java.io.Serializable;
import java.sql.SQLException;
import java.util.logging.Logger;

import javax.naming.NamingException;
import javax.naming.Reference;
import javax.naming.Referenceable;
import javax.sql.XAConnection;
import javax.sql.XADataSource;

/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 3.0
 */

public class CUBRIDXADataSource extends CUBRIDPoolDataSourceBase implements
		XADataSource, Referenceable, Serializable {
    	private static final long serialVersionUID = -7015869630223848825L;

	public CUBRIDXADataSource() {
		super();
	}

	protected CUBRIDXADataSource(Reference ref) {
		super();
		setProperties(ref);
	}

	/*
	 * javax.sql.XADataSource interface
	 */

	public synchronized XAConnection getXAConnection() throws SQLException {
		return getXAConnection(getUser(), getPassword());
	}

	public synchronized XAConnection getXAConnection(String username,
			String passwd) throws SQLException {
		return (new CUBRIDXAConnection(this, getServerName(), getPortNumber(),
				getDatabaseName(), username, passwd));
	}

	/*
	 * javax.naming.Referenceable interface
	 */

	public synchronized Reference getReference() throws NamingException {
		Reference ref = new Reference(this.getClass().getName(),
				"cubrid.jdbc.driver.CUBRIDDataSourceObjectFactory", null);

		ref = getProperties(ref);
		return ref;
	}

	/* JDK 1.7 */
	public Logger getParentLogger() {
		throw new java.lang.UnsupportedOperationException();
	}
}
