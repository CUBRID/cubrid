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
package com.cubrid.cubridmanager.core.common.task;

/**
 * 
 * This class list a lot of common send messages,these messages will be used by
 * CommonQueryTask and CommonUpdateTask class.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CommonSendMsg {

	public static final String[] commonSimpleSendMsg = new String[] { "task",
			"token" };

	/**
	 * taskName:compactdb,checkdb,dbspaceinfo,userinfo,getloginfo,stopdb,checkdb,lockInfo
	 * 
	 */
	public static final String[] commonDatabaseSendMsg = new String[] { "task",
			"token", "dbname" ,"repairdb"};
	/**
	 * taskName:optimizedb
	 * 
	 */
	public static final String[] optimizeDbSendMsg = new String[] { "task",
			"token", "dbname", "classname" };

	/**
	 * taskName:deletedb
	 */
	public static final String[] deletedbSendMsg = new String[] { "task",
			"token", "dbname", "delbackup" };

	/**
	 * taskName:classinfo
	 */
	public static final String[] classInfoSendMsg = new String[] { "task",
			"token", "dbname", "dbstatus" };

	/**
	 * taskName:getlogfileinfo
	 */
	public static final String[] getBrokerLogFileInfoMSGItems = new String[] {
			"task", "token", "broker" };

	/**
	 * taskName:killtransaction
	 */
	public static final String[] killTransactionMSGItems = new String[] {
			"task", "token", "dbname", "type", "parameter" };

	/**
	 * taskName:deleteuser
	 */
	public static final String[] deleteUserMSGItems = new String[] { "task",
			"token", "dbname", "username" };
	/**
	 * taskName:getbrokerstatus
	 * 
	 */
	public static final String[] getBrokerStatusItems = new String[] { "task",
			"token", "bname" };
}
