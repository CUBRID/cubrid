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

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.List;
import java.util.Map;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.CommonTool;
import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.ServerManager;
import com.cubrid.cubridmanager.core.common.StringUtil;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;

/**
 * 
 * This class is abstract,it provide base methods to communicate with CUBRID
 * Manager server,all concrete task must extend it and finish a lot of concrete
 * operations. Every instance of this class is used in a single thread at
 * best.when multiple thread use the same instance,may cause sent message
 * confusion.
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-4 created by moulinwang
 */
public abstract class SocketTask implements
		ITask {
	private static final Logger logger = LogUtil.getLogger(SocketTask.class);
	protected String taskName = "";
	protected MessageMap sendedMsgMap;
	protected ServerInfo serverInfo;
	protected boolean isCancel = false;

	protected boolean isUsingMonPort = false;
	protected ClientSocket clientSocket = null;
	// whether the responsed message used speical delimiter
	protected boolean isUsingSpecialDelimiter = false;
	// whether this task need to send message multi using the same socket
	protected boolean isNeedMultiSend = false;
	// Before send message,whether need server connected status
	protected boolean isNeedServerConnected = true;
	protected String errorMsg = null;
	protected String warningMsg = null;
	protected String appendSendMsg = null;

	/**
	 * 
	 * The constructor
	 * 
	 * @param taskName
	 * @param serverInfo
	 */
	protected SocketTask(String taskName, ServerInfo serverInfo) {
		this(taskName, serverInfo, null);
	}

	/**
	 * The constructor
	 * 
	 * @param taskName
	 * @param serverInfo
	 * @param sendedOrderMsgItems
	 */
	protected SocketTask(String taskName, ServerInfo serverInfo,
			String[] sendedOrderMsgItems) {
		this.taskName = taskName;
		this.serverInfo = serverInfo;
		sendedMsgMap = new MessageMap(sendedOrderMsgItems);
		if (serverInfo != null)
			clientSocket = new ClientSocket(serverInfo.getHostAddress(),
					serverInfo.getHostJSPort());
	}

	/**
	 * Set sending message information, assign only single value to the key,task
	 * or token is not needed setting
	 * 
	 * @param key
	 * @param value
	 */
	protected void setMsgItem(String key, String value) {
		sendedMsgMap.addOrModifyValue(key, value);
	}

	/**
	 * Set sending message information, assign more than one values to the
	 * key,task or token is not needed setting
	 * 
	 * @param key
	 * @param value
	 */
	protected void setMsgItem(String key, String[] values) {
		sendedMsgMap.addOrModifyValues(key, values);
	}

	/**
	 * Send message to CBURID Manager server
	 */
	public void execute() {
		isCancel = false;
		errorMsg = null;
		warningMsg = null;
		if (clientSocket == null || serverInfo == null) {
			errorMsg = Messages.error_noInitSocket;
			return;
		}
		if (isNeedServerConnected
				&& !ServerManager.getInstance().isConnected(
						serverInfo.getHostAddress(),
						serverInfo.getHostMonPort())) {
			errorMsg = Messages.error_disconnected;
			return;
		}
		String message = "";

		/** ***add by robin****** */
		if (null != appendSendMsg && !"".equals(appendSendMsg)) {

			if (taskName != null && taskName.length() > 0)
				message += "task:" + taskName + "\n";
			if (serverInfo.getHostToken() != null
					&& serverInfo.getHostToken().length() > 0)
				message += "token:" + serverInfo.getHostToken() + "\n";
			message += appendSendMsg + "\n\n";
		} else
			message = getRequest();

		clientSocket.setUsingSpecialDelimiter(isUsingSpecialDelimiter);
		clientSocket.sendRequest(message);
		if (!isNeedMultiSend) {
			clientSocket.tearDownConnection();
		}
		errorMsg = clientSocket.getErrorMsg();
		warningMsg = clientSocket.getWarningMsg();
	}

	/**
	 * 
	 * Return the request message
	 * 
	 * @return
	 */
	public String getRequest() {
		if (taskName != null && taskName.length() > 0)
			sendedMsgMap.addOrModifyValue("task", taskName);
		if (serverInfo.getHostToken() != null
				&& serverInfo.getHostToken().length() > 0)
			sendedMsgMap.addOrModifyValue("token", serverInfo.getHostToken());
		return sendedMsgMap.toString();
	}

	private static void invokeMethod4SetValue(Method m, Class<?> parameters,
			Object targetObject, Object value) {
		try {

			if (parameters == String.class) {
				m.invoke(targetObject, value);
			} else if (parameters == int.class) {
				m.invoke(targetObject, CommonTool.str2Int((String) value));
			} else if (parameters == boolean.class) {
				m.invoke(targetObject, CommonTool.strYN2Boolean((String) value));
			} else if (parameters == double.class) {
				m.invoke(targetObject, CommonTool.str2Double((String) value));
			} else if (parameters == byte.class) {
				m.invoke(targetObject, CommonTool.str2Int((String) value));
			} else if (parameters == String[].class) {
				m.invoke(targetObject, value);
			}

			// Just record below log for debug
		} catch (IllegalArgumentException e) { // error argument
			// type
			logger.error(e.getMessage(), e);
		} catch (IllegalAccessException e) { // no access right
			logger.error(e.getMessage(), e);
		} catch (InvocationTargetException e) { // error invoke
			// method
			// exception
			logger.error(e.getTargetException().getMessage(), e);
		}
	}

	/**
	 * Set a target object's fields' value by a Treenode object
	 * 
	 * @param node
	 * @param targetObject
	 */
	public static void setFieldValue(TreeNode node, Object targetObject) {
		Method[] methods = targetObject.getClass().getDeclaredMethods();
		for (Method m : methods) {
			String methodname = m.getName();
			Class<?>[] parameters = m.getParameterTypes();
			String field = methodname.substring(3).toString();
			if (methodname.startsWith("set") && parameters != null
					&& parameters.length == 1) {
				String value = node.getValue(field.toLowerCase());
				String[] values = node.getValues(field.toLowerCase());
				if (value != null && values.length == 1) {
					invokeMethod4SetValue(m, parameters[0], targetObject, value);
				}
				if (values != null && values.length > 1) {
					invokeMethod4SetValue(m, parameters[0], targetObject,
							values);
				}
			} else if (methodname.startsWith("add") && parameters != null
					&& parameters.length == 1) {
				String[] values = node.getValues(field.toLowerCase());
				if (values != null) {
					for (String value : values) {
						invokeMethod4SetValue(m, parameters[0], targetObject,
								value);
					}
				} else {
					List<TreeNode> children = node.getChildren();
					if (null != children) {
						for (TreeNode n : children) {
							String nodeName = n.getValue("open");
							if (nodeName == null
									|| nodeName.trim().length() <= 0) {
								nodeName = n.getValue("start");
							}
							if (field.equalsIgnoreCase(nodeName)) {
								try {
									Class<?> clazz = parameters[0];
									Object o;
									if (clazz == Map.class) {
										o = n.getValueByMap();
									} else {
										o = clazz.newInstance();
										setFieldValue(n, o);
									}
									m.invoke(targetObject, o);
								} catch (InstantiationException e) {
									logger.error(e.getMessage(), e);
								} catch (IllegalAccessException e) {
									logger.error(e.getMessage(), e);
								} catch (IllegalArgumentException e) {
									logger.error(e.getMessage(), e);
								} catch (InvocationTargetException e) {
									logger.error(e.getMessage(), e);
								}

							}
						}
					}
				}
			}

		}
	}

	/**
	 * Add a array of String to a list of String
	 * 
	 * @param list List<String>
	 * @param values String[]
	 */
	public static void fillSet(List<String> list, String[] values) {
		if (values != null && values.length > 0) {
			for (String value : values) {
				list.add(value);
			}
		}
	}

	/**
	 * 
	 * Stop running this task
	 * 
	 */
	public void cancel() {
		isCancel = true;
		if (clientSocket != null) {
			clientSocket.stopRead();
			clientSocket.tearDownConnection();
		}
	}

	/**
	 * 
	 * Get error message after this task execute.if it is null,this task is
	 * ok,or it has error
	 * 
	 * @return
	 */
	public String getErrorMsg() {
		return errorMsg;
	}

	/**
	 * 
	 * Get warning message after this task execute
	 * 
	 * @return
	 */
	public String getWarningMsg() {
		return warningMsg;
	}

	/**
	 * 
	 * Get the name of this task
	 * 
	 * @return
	 */
	public String getTaskname() {
		return taskName;
	}

	/**
	 * 
	 * Set the name of this task
	 * 
	 * @param taskname
	 */
	public void setTaskname(String taskname) {
		this.taskName = taskname;
	}

	/**
	 * 
	 * Set sent message order
	 * 
	 * @param orders
	 */
	protected void setOrders(String[] orders) {
		sendedMsgMap.setOrders(orders);
	}

	/**
	 * 
	 * Get the result after this task execute
	 * 
	 * @return
	 */
	protected TreeNode getResponse() {
		return clientSocket != null ? clientSocket.getResponse() : null;
	}

	/**
	 * 
	 * Get whether this task use special delimiter
	 * 
	 * @return
	 */
	public boolean isUsingSpecialDelimiter() {
		return isUsingSpecialDelimiter;
	}

	/**
	 * 
	 * Set whether this task use special delimiter
	 * 
	 * @param usingSpecialDelimiter
	 */
	public void setUsingSpecialDelimiter(boolean usingSpecialDelimiter) {
		this.isUsingSpecialDelimiter = usingSpecialDelimiter;
	}

	/**
	 * 
	 * Get CUBRID Manager server information
	 * 
	 * @return
	 */
	public ServerInfo getServerInfo() {
		return serverInfo;
	}

	/**
	 * 
	 * Set CUBRID Manager server information
	 * 
	 * @param serverInfo
	 */
	public void setServerInfo(ServerInfo serverInfo) {
		this.serverInfo = serverInfo;
		if (serverInfo != null && clientSocket == null)
			clientSocket = new ClientSocket(serverInfo.getHostAddress(),
					isUsingMonPort ? serverInfo.getHostMonPort()
							: serverInfo.getHostJSPort());
		else if (serverInfo != null && clientSocket != null) {
			clientSocket.setHostAddress(serverInfo.getHostAddress());
			clientSocket.setPort(isUsingMonPort ? serverInfo.getHostMonPort()
					: serverInfo.getHostJSPort());
		}
	}

	/**
	 * 
	 * Get whether this task is for monitoring connection continuously
	 * 
	 * @return
	 */
	public boolean isUsingMonPort() {
		return isUsingMonPort;
	}

	/**
	 * 
	 * Set whether this task use monitoring port
	 * 
	 * @param isMonitorConnection
	 */
	public void setUsingMonPort(boolean isUsingMonPort) {
		this.isUsingMonPort = isUsingMonPort;
		if (clientSocket != null) {
			clientSocket.setUsingMonitorPort(true);
			clientSocket.setPort(isUsingMonPort ? serverInfo.getHostMonPort()
					: serverInfo.getHostJSPort());
		}
	}

	/**
	 * 
	 * Get whether this task use monitoring port
	 * 
	 * @return
	 */
	protected ClientSocket getClientSocket() {
		return clientSocket;
	}

	/**
	 * 
	 * Set the executed socket of this task
	 * 
	 * @param clientSocket
	 */
	protected void setClientSocket(ClientSocket clientSocket) {
		this.clientSocket = clientSocket;
	}

	/**
	 * 
	 * Clear sent message
	 * 
	 */
	public void clearMsgItems() {
		if (sendedMsgMap != null) {
			sendedMsgMap.clear();
		}
	}

	/**
	 * Get response message of this task
	 * 
	 * @return
	 */
	protected String getResponsedMsg() {
		return clientSocket != null ? clientSocket.getResponsedMsg()
				: Messages.error_noInitSocket;
	}

	/**
	 * Return whether to need to send message multiple times using the same
	 * socket
	 * 
	 * @return
	 */
	public boolean isNeedMultiSend() {
		return isNeedMultiSend;
	}

	/**
	 * Set whether to need to send message multiple times using the same socket
	 * 
	 * @param isNeedMultiSend
	 */
	public void setNeedMultiSend(boolean isNeedMultiSend) {
		this.isNeedMultiSend = isNeedMultiSend;
	}

	/**
	 * When need to send message multiple times using the same socket,at last
	 * need to call this method to close this socket
	 */
	public void finish() {
		if (clientSocket != null) {
			clientSocket.tearDownConnection();
		}
	}

	/**
	 * When this task send message,return whether to need that server is
	 * connected.
	 * 
	 * @return
	 */
	public boolean isNeedServerConnected() {
		return isNeedServerConnected;
	}

	/**
	 * When this task send message,set whether to need that server is connected.
	 * 
	 * @param isNeedServerConnected
	 */
	public void setNeedServerConnected(boolean isNeedServerConnected) {
		this.isNeedServerConnected = isNeedServerConnected;
	}

	/**
	 * 
	 * Set error message
	 * 
	 * @param errorMsg
	 */
	public void setErrorMsg(String errorMsg) {
		this.errorMsg = errorMsg;
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

	public String getAppendSendMsg() {
		return appendSendMsg;
	}

	public void setAppendSendMsg(String appendSendMsg) {
		this.appendSendMsg = appendSendMsg;
	}

	public boolean isCancel() {
		return isCancel;
	}

	public boolean isSuccess() {
		TreeNode node = this.getResponse();
		if (node == null)
			return false;
		return StringUtil.isEqual(node.getValue("status"), "success");
	}

}
