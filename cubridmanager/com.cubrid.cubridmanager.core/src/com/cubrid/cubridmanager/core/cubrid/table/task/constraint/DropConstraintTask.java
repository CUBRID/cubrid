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
package com.cubrid.cubridmanager.core.cubrid.table.task.constraint;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.AttributeCategory;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ConstraintType;

/**
 * This class is to drop an constraint to a class in CUBRID database,including
 * primary key,foreign key,unique,not null four constraints
 * 
 * Usage: You must first set dbname, classname, type, name, one or more
 * attributes, and category fields, then call sendMsg() method to send a request
 * message, the response message is the information of the special class.
 * 
 * @author moulinwang 2009-3-2
 */
public class DropConstraintTask extends SocketTask {


	private final static String[] sendMSGItems = new String[]
		{
		        "task",
		        "token",
		        "dbname",
		        "classname",
		        "type",
		        "name",
		        "attributecount",
		        "attribute",
		        "category",
		};
	TreeNode dropConstraintTaskResponse;

	public DropConstraintTask(ServerInfo serverInfo) {
		super("dropconstraint", serverInfo, DropConstraintTask.sendMSGItems);
	}

	/**
	 * Set the key "dbname" in request message
	 * 
	 * @param dbname
	 */
	public void setDbName(String dbname) {
		this.setMsgItem("dbname", dbname);
	}

	/**
	 * Set the key "classname" in request message
	 * 
	 * @param className
	 */
	public void setClassName(String className) {
		this.setMsgItem("classname", className);
	}

	/**
	 * Set the key "type" in request message,support 4 constraints including
	 * INDEX,UNIQUE,FOREIGN KEY,PRIMARY KEY
	 * 
	 * @param type
	 */
	public void setType(ConstraintType type) {
		setMsgItem("type", type.getText());
	}

	/**
	 * Set the key "name" in request message, which indicates a index name, a
	 * unique constraint name, a foreign key name or a primary key name
	 * 
	 * @param name
	 */
	public void setName(String name) {
		this.setMsgItem("name", name);
	}

	/**
	 * Set the key "attribute" and "attributecount" in request message
	 * 
	 * @param name
	 */
	public void setAttributes(String[] attributes) {
		assert (attributes != null && attributes.length > 0);
		this.setMsgItem("attribute", attributes);
		this.setMsgItem("attributecount", "" + attributes.length);
	}

	/**
	 * Set the key "category" in request message,only support "instance" and
	 * "class" two types
	 * 
	 * @param category
	 */
	public void setCategory(AttributeCategory category) {
		this.setMsgItem("category", category.getText());
	}

	/**
	 * Get the SchemaInfo object from the response
	 * 
	 * @return
	 */
	public SchemaInfo getSchemaInfo() {
		TreeNode classinfo = dropConstraintTaskResponse.getChildren().get(0);
		return ModelUtil.getSchemaInfo(classinfo);
	}

}
