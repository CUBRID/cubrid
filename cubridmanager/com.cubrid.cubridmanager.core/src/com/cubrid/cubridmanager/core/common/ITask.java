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
package com.cubrid.cubridmanager.core.common;

/**
 * 
 * This task is responsible to communite with CUBRID Server by socket or
 * JDBC.All task must implement this interface
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public interface ITask {

	/**
	 * 
	 * Send request to Server
	 * 
	 */
	public void execute();

	/**
	 * 
	 * Return error message
	 * 
	 * @return
	 */
	public String getErrorMsg();

	/**
	 * 
	 * Set error message
	 * 
	 * @param errorMsg
	 */
	public void setErrorMsg(String errorMsg);

	/**
	 * 
	 * Return warning message
	 * 
	 * @return
	 */
	public String getWarningMsg();

	/**
	 * 
	 * Set warning message
	 * 
	 * @param waringMsg
	 */
	public void setWarningMsg(String waringMsg);

	/**
	 * 
	 * Return task name
	 * 
	 * @return
	 */
	public String getTaskname();

	/**
	 * 
	 * Set task name
	 * 
	 * @param taskName
	 */
	public void setTaskname(String taskName);

	/**
	 * 
	 * Cancel this operation
	 * 
	 */
	public void cancel();

	/**
	 * 
	 * End this task and free resource
	 * 
	 */
	public void finish();
	
	/**
	 *Get the flag of is cancel of this task
	 * 
	 * @return
	 */
	public boolean isCancel();
	
	/**
	 * Wether this task is success
	 * 
	 * @return
	 */
	public boolean isSuccess();
}
