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
package com.cubrid.cubridmanager.core.cubrid.table.task.attribute;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.AttributeCategory;

/**
 * This class is to add an attribute to a class in CUBRID database
 * 
 * Usage: You must first set dbname, classname, attributename, type, default,
 * category, unique, and notnull fields, then call sendMsg() method to send a
 * request message, the response message is the information of the special
 * class.
 * 
 * @author moulinwang 2009-3-2
 */
public class AddAttributeTask extends SocketTask {
	private final static String[] sendMSGItems = new String[]
		{
		        "task",
		        "token",
		        "dbname",
		        "classname",
		        "attributename",
		        "type",
		        "default",
		        "category",
		        "unique",
		        "notnull"
		};
	public AddAttributeTask(ServerInfo serverInfo) {
		super("addattribute", serverInfo, AddAttributeTask.sendMSGItems);
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
	 * Set the key "attributename" in request message
	 * 
	 * @param attributeName
	 */
	public void setAttributeName(String attributeName) {
		this.setMsgItem("attributename", attributeName);
	}

	/**
	 * Set the key "type" in request message eg: CHAR(2)
	 * 
	 * @param type
	 */
	public void setType(String type) {
		this.setMsgItem("type", type);
	}

	/**
	 * Set the key "default" in request message eg: null
	 * 
	 * @param defaultString
	 */
	public void setDefault(String defaultString) {
		if (defaultString == null) {
			this.setMsgItem("default", "");
		} else {
			this.setMsgItem("default", defaultString);
		}

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
	 * Set the key "unique" in request message, receive a boolean type and
	 * transform into "yes" or "no"
	 * 
	 * @param unique
	 *            boolean
	 */
	public void setUnique(boolean unique) {
		if (unique) {
			this.setMsgItem("unique", "yes");
		} else {
			this.setMsgItem("unique", "no");
		}
	}

	/**
	 * Set the key "notnull" in request message,receive a boolean type and
	 * transform into "yes" or "no"
	 * 
	 * @param notnull
	 *            boolean
	 */
	public void setNotNull(boolean notnull) {
		if (notnull) {
			this.setMsgItem("notnull", "yes");
		} else {
			this.setMsgItem("notnull", "no");
		}
	}

	/**
	 * Get the SchemaInfo object from the response
	 * 
	 * @return
	 */
	public SchemaInfo getSchemaInfo() {
		TreeNode classinfo = getResponse().getChildren().get(0);
		return ModelUtil.getSchemaInfo(classinfo);
	}

}
