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
package com.cubrid.cubridmanager.ui.spi.progress;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;

import com.cubrid.cubridmanager.core.common.ITask;

/**
 * 
 * A abstract class that has <code>Task</code> array and run method in order
 * to execute a specific function. Generally,subclasses should be an inner class
 * in a dialog and implements the <code>exec<code> method
 * 
 * @see com.cubrid.cubridmanager.core.common.socket.SocketTask
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-3-30 created by lizhiqiang
 */
public abstract class TaskExecutor {
	protected boolean success;
	protected List<ITask> taskList = new ArrayList<ITask>();

	/**
	 * This method is to be complete the concrete task.When the task be canceled
	 * by user or the task have completed, the return value should be
	 * <code>false</code> to indicate that the thread is stopped,or it should
	 * be default value<code>true</code>. Generally,the subclasses should
	 * override this method .an instance as follows:
	 * 
	 * <code>
	 *     private class BackupPlanTaskExec extends  TaskExecutor{
	 *      public boolean exec(final IProgressMonitor monitor) {
	 *       boolean isSuccess = true;
	 *       Display display = getShell().getDisplay();
	 *		  if (monitor.isCanceled()) {
	 *			isSuccess = false;
	 *			return isSuccess;
	 *		 }
	 *		
	 *		for (Task task :taskList) {
	 *			task.sendMsg();
	 *			final String msg = task.getErrorMsg();
	 *			if (monitor.isCanceled()) {
	 *				return false;
	 *			}
	 *			if (msg != null && msg.length() > 0 && !monitor.isCanceled()) {
	 *				display.syncExec(new Runnable() {
	 *					public void run() {
	 *						CommonTool.openErrorBox(getShell(),  msg);
	 *					}
	 *				});
	 *				isSuccess = false;
	 *				return isSuccess;
	 *			}
	 *			if (monitor.isCanceled()) {
	 *				isSuccess = false;
	 *				return isSuccess;
	 *			}
	 *		}
	 *		if (!monitor.isCanceled()) {
	 *			display.syncExec(new Runnable() {
	 *				public void run() {					
	 *						setReturnCode(OK);
	 *						close();
	 *					}
	 *				}
	 *			);
	 *		}
	 *		return true;
	 *	}
	 * }
	 *</code>
	 * 
	 * @return
	 */
	public abstract boolean exec(final IProgressMonitor monitor);

	/**
	 * Calls the cancel method in <code>Task</code> class in order to stop the
	 * socket connection.
	 * 
	 * @see com.cubrid.cubridmanager.core.common.socket#cancel()
	 * 
	 */
	public void cancel() {
		for (ITask task : taskList) {
			if (null != task)
				task.cancel();
		}

	}

	/**
	 * Sets the concrete task.
	 * 
	 * @param task
	 */
	public void setTask(ITask[] tasks) {
		if (tasks != null && tasks.length > 0)
			taskList.addAll(Arrays.asList(tasks));
	}

	/**
	 * Adds the task
	 * 
	 * @param task
	 */
	public void addTask(ITask task) {
		if (!taskList.contains(task)) {
			taskList.add(task);
		}
	}

	/**
	 * realse all Task
	 * 
	 * @param task
	 */
	public void releaseTask() {
		taskList = new ArrayList<ITask>();
	}
	
	public boolean isSuccess(){
		return success;
	}

}
