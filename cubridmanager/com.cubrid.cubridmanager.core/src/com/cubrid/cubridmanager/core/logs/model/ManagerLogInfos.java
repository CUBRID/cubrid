/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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

package com.cubrid.cubridmanager.core.logs.model;

/**
 * 
 * This class is responsible to store managerloginfos
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-4-3 created by wuyingshi
 */
public class ManagerLogInfos {

	private ManagerLogInfoList accessLog = null;
	private ManagerLogInfoList errorLog = null;

	/**
	 * The constructor
	 */
	public ManagerLogInfos() {
		accessLog = new ManagerLogInfoList();
		errorLog = new ManagerLogInfoList();
	}

	/**
	 * return the accessLog.
	 * 
	 * @return
	 */
	public ManagerLogInfoList getAccessLog() {
		return accessLog;
	}

	/**
	 * set the accessLog.
	 * 
	 * @param accessLog
	 */
	public void setAccessLog(ManagerLogInfoList accessLog) {
		this.accessLog = accessLog;
	}

	/**
	 * get the errorLog.
	 * 
	 * @return
	 */
	public ManagerLogInfoList getErrorLog() {
		return errorLog;
	}

	/**
	 * set the errorLog.
	 * 
	 * @param errorLog
	 */
	public void setErrorLog(ManagerLogInfoList errorLog) {
		this.errorLog = errorLog;
	}

}
