/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution. 
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
package com.cubrid.cubridmanager.ui.broker.dialog.internal;

/**
 * A plain class
 * 
 * @author lizhiqiang 2009-3-31
 */
public class Broker {
	private String name;
	private String service;
	private int brokerPort;
	private int minNumApplServer;
	private int maxNumApplServer;
	private int applServerShmId;
	private int applServerMaxSize;
	private String logDir;
	private String errorLogDir;
	private String sqlLog;
	private int timeToKill;
	private int sessionTimeout;
	private String keepConnection;

	/**
	 * Gets the name of the current broker instance
	 * 
	 * @return the name
	 */
	public String getName() {
		return name;
	}

	/**
	 * Sets the name of the broker
	 * 
	 * @param name the name to set
	 */
	public void setName(String name) {
		this.name = name;
	}

	/**
	 * Gets the service status
	 * 
	 * @return the service
	 */
	public String getService() {
		return service;
	}

	/**
	 * Sets the service status
	 * 
	 * @param service the service to set
	 */
	public void setService(String service) {
		this.service = service;
	}

	/**
	 * Gets the broker port value
	 * 
	 * @return the brokerPort
	 */
	public int getBrokerPort() {
		return brokerPort;
	}

	/**
	 * Sets the broker port value
	 * 
	 * @param brokerPort the brokerPort to set
	 */
	public void setBrokerPort(int brokerPort) {
		this.brokerPort = brokerPort;
	}

	/**
	 * Gets the minimum of application server
	 * 
	 * @return the minNumApplServer
	 */
	public int getMinNumApplServer() {
		return minNumApplServer;
	}

	/**
	 * Sets the minimum of application server
	 * 
	 * @param minNumApplServer the minNumApplServer to set
	 */
	public void setMinNumApplServer(int minNumApplServer) {
		this.minNumApplServer = minNumApplServer;
	}

	/**
	 * Gets the maximum of application server
	 * 
	 * @return the maxNumApplServer
	 */
	public int getMaxNumApplServer() {
		return maxNumApplServer;
	}

	/**
	 * Sets the maximum of application server
	 * 
	 * @param maxNumApplServer the maxNumApplServer to set
	 */
	public void setMaxNumApplServer(int maxNumApplServer) {
		this.maxNumApplServer = maxNumApplServer;
	}

	/**
	 * Gets the SHM id of application server
	 * 
	 * @return the applServerShmId
	 */
	public int getApplServerShmId() {
		return applServerShmId;
	}

	/**
	 * Sets the SHM id of application server
	 * 
	 * @param applServerShmId the applServerShmId to set
	 */
	public void setApplServerShmId(int applServerShmId) {
		this.applServerShmId = applServerShmId;
	}

	/**
	 * Gets the maximum size of application server
	 * 
	 * @return the applServerMaxSize
	 */
	public int getApplServerMaxSize() {
		return applServerMaxSize;
	}

	/**
	 * Sets the maximum size of application server
	 * 
	 * @param applServerMaxSize the applServerMaxSize to set
	 */
	public void setApplServerMaxSize(int applServerMaxSize) {
		this.applServerMaxSize = applServerMaxSize;
	}

	/**
	 * Gets the log directory
	 * 
	 * @return the logDir
	 */
	public String getLogDir() {
		return logDir;
	}

	/**
	 * Sets the log directory
	 * 
	 * @param logDir the logDir to set
	 */
	public void setLogDir(String logDir) {
		this.logDir = logDir;
	}

	/**
	 * Gets the error log directory
	 * 
	 * @return the errorLogDir
	 */
	public String getErrorLogDir() {
		return errorLogDir;
	}

	/**
	 * Sets the error log directory
	 * 
	 * @param errorLogDir the errorLogDir to set
	 */
	public void setErrorLogDir(String errorLogDir) {
		this.errorLogDir = errorLogDir;
	}

	/**
	 * Gets the sqlLog
	 * 
	 * @return the sqlLog
	 */
	public String getSqlLog() {
		return sqlLog;
	}

	/**
	 * Sets the sqlLog
	 * 
	 * @param sqlLog the sqlLog to set
	 */
	public void setSqlLog(String sqlLog) {
		this.sqlLog = sqlLog;
	}

	/**
	 * Gets the time to kill
	 * 
	 * @return the timeToKill
	 */
	public int getTimeToKill() {
		return timeToKill;
	}

	/**
	 * Sets the time to kill
	 * 
	 * @param timeToKill the timeToKill to set
	 */
	public void setTimeToKill(int timeToKill) {
		this.timeToKill = timeToKill;
	}

	/**
	 * Gets the timeout of session
	 * 
	 * @return the sessionTimeout
	 */
	public int getSessionTimeout() {
		return sessionTimeout;
	}

	/**
	 * Sets the timeout of session
	 * 
	 * @param sessionTimeout the sessionTimeout to set
	 */
	public void setSessionTimeout(int sessionTimeout) {
		this.sessionTimeout = sessionTimeout;
	}

	/**
	 * Gets the way to keep connection
	 * 
	 * @return the keepConnection
	 */
	public String getKeepConnection() {
		return keepConnection;
	}

	/**
	 * Sets the way to keep connection
	 * 
	 * @param keepConnection the keepConnection to set
	 */
	public void setKeepConnection(String keepConnection) {
		this.keepConnection = keepConnection;
	}

}
