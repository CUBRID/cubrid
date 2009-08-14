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
 * This class is to update an attribute to a class in CUBRID database
 * 
 * Usage: You must first set dbname, classname, oldmethodname, newmethodname,
 * category, index, notnull, unique, and default fields, then call sendMsg()
 * method to send a request message, the response message is the information of
 * the special class.
 * 
 * @author moulinwang 2009-3-2
 */
public class UpdateAttributeTask extends SocketTask {
	private final static String[] sendMSGItems = new String[]
		{
		        "task",
		        "token",
		        "dbname",
		        "classname",
		        "oldattributename",
		        "newattributename",
		        "category",
		        "index",
		        "notnull",
		        "unique",
		        "default"
		};
	TreeNode updateAttributeTaskResponse;

	/**
	 * @param args
	 */
	public UpdateAttributeTask(ServerInfo serverInfo) {
		super("updateattribute", serverInfo, sendMSGItems);
		// TODO to align with the old source(8.1.4),without this line, the cm server
		// will break down
		this.setMsgItem("index", "x");
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
	 * Set the key "oldattributename" in request message
	 * 
	 * @param oldAttributeName
	 */
	public void setOldAttributeName(String oldAttributeName) {
		this.setMsgItem("oldattributename", oldAttributeName);
	}

	/**
	 * Set the key "newattributename" in request message
	 * 
	 * @param newAttributeName
	 */
	public void setNewAttributeName(String newAttributeName) {
		this.setMsgItem("newattributename", newAttributeName);
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
	 * Set the key "notnull" in request message,receive a boolean type and
	 * transform into "y" or "n"
	 * 
	 * @param notnull
	 *            boolean
	 */
	public void setNotNull(boolean notnull) {
		if (notnull) {
			this.setMsgItem("notnull", "y");
		} else {
			this.setMsgItem("notnull", "n");
		}
	}

	/**
	 * Set the key "unique" in request message, receive a boolean type and
	 * transform into "y" or "n"
	 * 
	 * @param unique
	 *            boolean
	 */
	public void setUnique(boolean unique) {
		if (unique) {
			this.setMsgItem("unique", "y");
		} else {
			this.setMsgItem("unique", "n");
		}
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
	 * Get the SchemaInfo object from the response
	 * 
	 * @return
	 */
	public SchemaInfo getSchemaInfo() {
		updateAttributeTaskResponse = this.getResponse();
		TreeNode classinfo = updateAttributeTaskResponse.getChildren().get(0);
		return ModelUtil.getSchemaInfo(classinfo);
	}

	public void dealWithResponse() {
		// TODO
		if (clientSocket.getErrorMsg() != null) {

		} else if (clientSocket.getWarningMsg() != null) {

		} else if (clientSocket.getResponse() != null) {

		}
	}

	// public SchemaInfo getSchemaInfo(TreeNode updateattribute) {
	// TreeNode classinfo = updateattribute.getChildren().get(0);
	// SchemaInfo table = new SchemaInfo();
	// setFieldValue(classinfo, table);
	// fillSet(table.superClasses, classinfo.getValues("superclass"));
	// fillSet(table.subClasses, classinfo.getValues("subclass"));
	// fillSet(table.methodFiles, classinfo.getValues("methodfile"));
	// fillSet(table.querySpecs, classinfo.getValues("queryspec"));
	// fillSet(table.oidList, classinfo.getValues("oid"));
	//
	// List<TreeNode> children = classinfo.getChildren();
	// for (TreeNode item : children) {
	// if (item.getValue("open").equals("attribute")) {
	// DBAttribute attribute = new DBAttribute();
	// setFieldValue(item, attribute);
	// table.attributes.add(attribute);
	// } else if (item.getValue("open").equals("classmethod")) {
	// DBMethod dm = new DBMethod();
	// setFieldValue(item, dm);
	// fillSet(dm.arguments, item.getValues("argument"));
	// table.classMethods.add(dm);
	// } else if (item.getValue("open").equals("method")) {
	// DBMethod dm = new DBMethod();
	// setFieldValue(item, dm);
	// fillSet(dm.arguments, item.getValues("argument"));
	// table.methods.add(dm);
	// } else if (item.getValue("open").equals("classresolution")) {
	// DBResolution dr = new DBResolution();
	// setFieldValue(item, dr);
	// table.classResolutions.add(dr);
	// } else if (item.getValue("open").equals("resolution")) {
	// DBResolution dr = new DBResolution();
	// setFieldValue(item, dr);
	// table.resolutions.add(dr);
	// } else if (item.getValue("open").equals("constraint")) {
	// Constraint cr = new Constraint();
	// setFieldValue(item, cr);
	// fillSet(cr.classAttributes, item.getValues("classattribute"));
	// fillSet(cr.attributes, item.getValues("attribute"));
	// fillSet(cr.rules, item.getValues("rule"));
	// table.constraints.add(cr);
	// }
	// }
	// return table;
	// }

}
