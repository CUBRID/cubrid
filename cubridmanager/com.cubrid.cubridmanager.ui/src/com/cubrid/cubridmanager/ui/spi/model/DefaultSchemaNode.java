/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.ui.spi.model;

/**
 * 
 * All database schema node must extend this class or use this class
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class DefaultSchemaNode extends
		DefaultCubridNode implements
		ISchemaNode {

	private CubridDatabase cubridDatabase = null;

	/**
	 * The constructor
	 * 
	 * @param id
	 * @param label
	 * @param iconPath
	 */
	public DefaultSchemaNode(String id, String label, String iconPath) {
		super(id, label, iconPath);
	}

	/**
	 * @see ISchemaNode#getDatabase()
	 */
	public CubridDatabase getDatabase() {
		return cubridDatabase;
	}

	/**
	 * @see ISchemaNode#setDatabase(CubridDatabase)
	 */
	public void setDatabase(CubridDatabase cubridDatabase) {
		this.cubridDatabase = cubridDatabase;
	}

	/**
	 * @see ICubridNode#addChild(ICubridNode)
	 */
	public void addChild(ICubridNode obj) {
		if (obj != null && obj instanceof ISchemaNode && !isContained(obj)) {
			ISchemaNode schemaNode = (ISchemaNode) obj;
			schemaNode.setDatabase(this.getDatabase());
		}
		super.addChild(obj);
	}
}
