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
package com.cubrid.cubridmanager.core.common.socket;

import java.io.BufferedInputStream;
import java.io.IOException;
import java.io.PrintWriter;
import java.net.Socket;
import java.net.UnknownHostException;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.ServerManager;
import com.cubrid.cubridmanager.core.common.log.LogUtil;

/**
 * 
 * To provide socket service for upper layer, send message, receive message and
 * parse message for upper layer to use. focus on:
 * <li>create a connect,and deal with unknownHostException
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-4 created by moulinwang
 */

public class ClientSocket {

	private static final Logger logger = LogUtil.getLogger(ClientSocket.class);
	/**
	 * return result
	 */
	private TreeNode response = null;
	private String responsedMsg = null;
	private String errorMsg = null;
	private String warningMsg = null;

	/**
	 * to stop reading from socket
	 */
	private boolean isStopRead = false;

	/**
	 * parse a message with special delimiter
	 */
	private boolean isUsingSpecialDelimiter = false;

	private BufferedInputStream socketInputStream;
	private PrintWriter socketWriter;

	private Socket socket;
	private String hostAddress;
	private int port;
	private boolean isUsingMonitorPort = false;
	/**
	 * when sending formal messages, the status is busy, in this case, heart
	 * beat is not needed at the same time.
	 */
	private boolean isBusy = true;
	/**
	 * a thread to send out the heart beat
	 */
	private Thread heartbitThread = null;

	/**
	 * The constructor
	 * 
	 * @param hostAddress
	 * @param port
	 */
	public ClientSocket(String hostAddress, int port) {
		this.hostAddress = hostAddress;
		this.port = port;
	}

	/**
	 * Set up a socket
	 * 
	 * @return
	 */
	private void setUpConnection() throws UnknownHostException, IOException {
		socket = new Socket(hostAddress, port);
		socket.setTcpNoDelay(false);
		socket.setKeepAlive(true);
		socketInputStream = new BufferedInputStream(socket.getInputStream());
		socketWriter = new PrintWriter(socket.getOutputStream());
	}

	/**
	 * 
	 * Send request to CUBRID Manager server
	 * 
	 * @param message
	 */
	public synchronized void sendRequest(String message) {
		try {
			// before sending a message, set busy=true
			isBusy = true;
			isStopRead = false;
			/**
			 * set up socket
			 */
			if (socket == null) {
				setUpConnection();
			}
			if (logger.isDebugEnabled())
				logger.debug("\n<sentMsg>\n" + message + "\n</sentMsg>\n");
			/**
			 * send a message
			 */
			if (socketWriter != null) {
				socketWriter.print(message);
				socketWriter.flush();
			}
			/**
			 * read the response and parse the message
			 */
			readResponse();

			// end parsing the response, set busy=false, so heart beat is OK
			// again
			isBusy = false;

		} catch (UnknownHostException e) {
			errorMsg = Messages.error_unknownHost;
			ServerManager.getInstance().setConnected(hostAddress,
					isUsingMonitorPort ? port : port - 1, false);
		} catch (IOException e) {
			errorMsg = Messages.error_connectfailed;
			ServerManager.getInstance().setConnected(hostAddress,
					isUsingMonitorPort ? port : port - 1, false);
		} catch (Exception e) {
			errorMsg = e.getMessage();
			ServerManager.getInstance().setConnected(hostAddress,
					isUsingMonitorPort ? port : port - 1, false);
		}
	}

	/**
	 * Read the response, check and parse it
	 */
	private void readResponse() {
		try {
			// initial return result
			response = null;
			responsedMsg = null;
			errorMsg = null;
			warningMsg = null;
			StringBuffer strBuffer = new StringBuffer();
			int len;
			byte tmp[] = new byte[2048];
			while (!isStopRead && socketInputStream != null
					&& (len = socketInputStream.read(tmp)) != -1) {
				strBuffer.append(new String(tmp, 0, len));
				if (strBuffer.indexOf("\n\n") == strBuffer.length() - 2)
					break;
			}
			responsedMsg = strBuffer.toString();
			if (logger.isDebugEnabled())
				logger.debug("\n<responsedMsg>\n" + responsedMsg
						+ "\n</responsedMsg>\n");
			checkParsedMsg(responsedMsg);
		} catch (IOException e) {
			errorMsg = Messages.error_connectfailed;
		}
	}

	/**
	 * 
	 * Stop reading responsed message
	 * 
	 */
	public synchronized void stopRead() {
		this.isStopRead = true;
	}

	/**
	 * Set the heart beat time in milliseconds, this method takes effect only at
	 * the first time. this method is to monitor whether the socket connection
	 * status is OK
	 * 
	 * @param time
	 */
	public synchronized void setHeartbeat(final int time) {
		if (heartbitThread == null) {
			heartbitThread = new Thread("Monitoring " + hostAddress + ":"
					+ (isUsingMonitorPort ? port : port - 1)) {
				public void run() {
					while (ServerManager.getInstance().isConnected(hostAddress,
							isUsingMonitorPort ? port : port - 1)) {
						try {
							if (logger.isDebugEnabled())
								logger.debug("The hearbeat thread "
										+ hostAddress + ":" + port
										+ " is running...");
							if (socket == null || socketInputStream == null) {
								ServerManager.getInstance().setConnected(
										hostAddress,
										isUsingMonitorPort ? port : port - 1,
										false);
								return;
							}
							if (!isBusy) {
								int length = socketInputStream.read();
								if (length < 0) {
									ServerManager.getInstance().setConnected(
											hostAddress,
											isUsingMonitorPort ? port
													: port - 1, false);
									return;
								}
							}
							sleep(time);
						} catch (IOException e) {
							ServerManager.getInstance().setConnected(
									hostAddress,
									isUsingMonitorPort ? port : port - 1, false);
							break;
						} catch (InterruptedException e) {
							ServerManager.getInstance().setConnected(
									hostAddress,
									isUsingMonitorPort ? port : port - 1, false);
							break;
						}
					}
					tearDownConnection();
				}
			};
			heartbitThread.start();
		}
	}

	/**
	 * Primary preprocess, check message format, produce tree structure
	 * 
	 * @param buf
	 */

	private void checkParsedMsg(String buf) {
		if ((isUsingSpecialDelimiter && buf.indexOf("\nEND__DIAGDATA\n") >= 0)
				|| (!isUsingSpecialDelimiter && buf.indexOf("\n\n") >= 0)) {
			if (buf.length() <= 16) {
				errorMsg = Messages.error_messageFormat;
				return;
			}
			int idx = buf.indexOf("open:special");
			if (idx >= 0) {
				String spmsg = buf.substring(idx + 13);
				spmsg = spmsg.substring(0, spmsg.length() - 15);
				warningMsg = spmsg;
				buf = buf.substring(0, idx) + "\n";
			}

			response = MessageUtil.parseResponse(buf, isUsingSpecialDelimiter);

			String task = response.getValue("task");
			String status = response.getValue("status");
			String note = response.getValue("note");
			if (task == null || status == null || note == null) {
				errorMsg = Messages.error_messageFormat;
			}
			if (status != null && !status.equals("success")) { // fail
				errorMsg = note;
			}
		}
	}

	/**
	 * Tear down the socket connection
	 */
	public synchronized void tearDownConnection() {
		try {
			if (socketWriter != null)
				socketWriter.close();
			socketWriter = null;
			if (socketInputStream != null)
				socketInputStream.close();
			socketInputStream = null;
			if (socket != null)
				socket.close();
			socket = null;
		} catch (IOException e) {
			logger.error(e);
		}
	}

	/**
	 * 
	 * Return parsed responsed node
	 * 
	 * @return
	 */
	public TreeNode getResponse() {
		return response;
	}

	/**
	 * Return the original responsed message
	 * 
	 * @return
	 */
	public String getResponsedMsg() {
		return responsedMsg;
	}

	/**
	 * 
	 * Return whether using special delimiter
	 * 
	 * @return
	 */
	public boolean isUsingSpecialDelimiter() {
		return isUsingSpecialDelimiter;
	}

	/**
	 * 
	 * Set whether using special demiliter
	 * 
	 * @param usingSpecialDelimiter
	 */
	public void setUsingSpecialDelimiter(boolean usingSpecialDelimiter) {
		this.isUsingSpecialDelimiter = usingSpecialDelimiter;
	}

	/**
	 * 
	 * Get error message
	 * 
	 * @return
	 */
	public String getErrorMsg() {
		return errorMsg;
	}

	/**
	 * 
	 * Get warning message
	 * 
	 * @return
	 */
	public String getWarningMsg() {
		return warningMsg;
	}

	/**
	 * 
	 * Set warning message
	 * 
	 * @param warningMsg
	 */
	public void setWarningMsg(String warningMsg) {
		this.warningMsg = warningMsg;
	}

	/**
	 * Get the host address of this socket
	 * 
	 * @return
	 */
	public synchronized String getHostAddress() {
		return hostAddress;
	}

	/**
	 * Set the host address of this socket
	 * 
	 * @param hostAddress
	 */
	public synchronized void setHostAddress(String hostAddress) {
		if (socket != null) {
			tearDownConnection();
		}
		this.hostAddress = hostAddress;
	}

	/**
	 * Get the port of this socket
	 * 
	 * @return
	 */
	public synchronized int getPort() {
		return port;
	}

	/**
	 * Set the port of this socket
	 * 
	 * @param port
	 */
	public synchronized void setPort(int port) {
		if (socket != null) {
			tearDownConnection();
		}
		this.port = port;
	}

	/**
	 * Get heart bit thread
	 * 
	 * @return
	 */
	public Thread getHeartbitThread() {
		return heartbitThread;
	}

	/**
	 * 
	 * Stop heart bit thread
	 * 
	 */
	public void stopHeartbitThread() {
		if (heartbitThread != null) {
			heartbitThread.interrupt();
			heartbitThread = null;
		}
	}

	/**
	 * 
	 * Return whether the monitor port is used
	 * 
	 * @return
	 */
	public synchronized boolean isUsingMonitorPort() {
		return isUsingMonitorPort;
	}

	/**
	 * 
	 * Set whether the monitor port is used
	 * 
	 * @return
	 */
	public synchronized void setUsingMonitorPort(boolean isUsingMonitorPort) {
		this.isUsingMonitorPort = isUsingMonitorPort;
	}

}
