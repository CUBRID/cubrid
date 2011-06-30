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

import javax.naming.Reference;
import javax.naming.StringRefAddr;

public class CUBRIDPoolDataSourceBase extends CUBRIDDataSourceBase {
	private int maxStatements;
	private int initialPoolSize;
	private int minPoolSize;
	private int maxPoolSize;
	private int maxIdleTime;
	private int propertyCycle;

	protected CUBRIDPoolDataSourceBase() {
		super();

		maxStatements = 0;
		initialPoolSize = 0;
		minPoolSize = 0;
		maxPoolSize = 0;
		maxIdleTime = 0;
		propertyCycle = 0;
	}

	public int getMaxStatements() {
		return maxStatements;
	}

	public int getInitialPoolSize() {
		return initialPoolSize;
	}

	public int getMinPoolSize() {
		return minPoolSize;
	}

	public int getMaxPoolSize() {
		return maxPoolSize;
	}

	public int getMaxIdleTime() {
		return maxIdleTime;
	}

	public int getPropertyCycle() {
		return propertyCycle;
	}

	public void setMaxStatements(int no) {
		maxStatements = 0;
	}

	public void setInitialPoolSize(int size) {
		initialPoolSize = size;
	}

	public void setMinPoolSize(int size) {
		minPoolSize = size;
	}

	public void setMaxPoolSize(int size) {
		maxPoolSize = size;
	}

	public void setMaxIdleTime(int interval) {
		maxIdleTime = interval;
	}

	public void setPropertyCycle(int interval) {
		propertyCycle = interval;
	}

	protected Reference getProperties(Reference ref) {
		ref = super.getProperties(ref);

		ref.add(new StringRefAddr("maxStatements", Integer
				.toString(getMaxStatements())));
		ref.add(new StringRefAddr("initialPoolSize", Integer
				.toString(getInitialPoolSize())));
		ref.add(new StringRefAddr("minPoolSize", Integer
				.toString(getMinPoolSize())));
		ref.add(new StringRefAddr("maxPoolSize", Integer
				.toString(getMaxPoolSize())));
		ref.add(new StringRefAddr("maxIdleTime", Integer
				.toString(getMaxIdleTime())));
		ref.add(new StringRefAddr("propertyCycle", Integer
				.toString(getPropertyCycle())));

		return ref;
	}

	protected void setProperties(Reference ref) {
		super.setProperties(ref);

		setMaxStatements(Integer.parseInt((String) ref.get("maxStatements")
				.getContent()));
		setInitialPoolSize(Integer.parseInt((String) ref.get("initialPoolSize")
				.getContent()));
		setMinPoolSize(Integer.parseInt((String) ref.get("minPoolSize")
				.getContent()));
		setMaxPoolSize(Integer.parseInt((String) ref.get("maxPoolSize")
				.getContent()));
		setMaxIdleTime(Integer.parseInt((String) ref.get("maxIdleTime")
				.getContent()));
		setPropertyCycle(Integer.parseInt((String) ref.get("propertyCycle")
				.getContent()));
	}
}
