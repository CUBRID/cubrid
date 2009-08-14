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
package com.cubrid.cubridmanager.core.cubrid.table.task.superclass;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.SuperClass;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil;

/**
 * This class is to get information of a class in CUBRID database, whether it is
 * a class or virtual class(view).
 * 
 * Usage: You must first set dbname and classname by invoking setDbName(String)
 * and setClassName(String) methods, then call sendMsg() method to send a
 * request message, the response message is the information of the special
 * class.
 * 
 * @author moulinwang 2009-3-2
 */
public class GetSuperClassesInfoTask extends SocketTask {

	private final static String[] sendMSGItems = new String[]
		{
		        "task",
		        "token",
		        "dbname",
		        "classname",
		};

	public GetSuperClassesInfoTask(ServerInfo serverInfo) {
		super("getsuperclassesinfo", serverInfo, GetSuperClassesInfoTask.sendMSGItems);
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
	 * Get the SchemaInfo object from the response
	 * 
	 * @return
	 */
	public List<SuperClass> getSuperClassList() {
		List<TreeNode> superclassesinfo = getResponse().getChildren();
		if(null!=superclassesinfo){

			List<TreeNode> classes = superclassesinfo.get(0).getChildren();
			if(null!=classes){
				List<SuperClass> list=new ArrayList<SuperClass>();
				for(TreeNode node:classes){
					SuperClass superclass=ModelUtil.getSuperClass(node);
					list.add(superclass);					
				}
				return list;
			}
		}
		return null;
	}
}
