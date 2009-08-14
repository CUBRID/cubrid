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

import java.lang.reflect.InvocationTargetException;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.ProgressMonitorDialog;
import org.eclipse.jface.operation.IRunnableWithProgress;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.ui.spi.Messages;

/**
 * 
 * An operation approver that implements the Type IRunnableWithProgress and
 * executes the task by Type <link>#TaskExecutor</link>.
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-4-3 created by lizhiqiang
 */
public class ExecTaskWithProgress implements
		IRunnableWithProgress {

	private static final Logger logger = LogUtil.getLogger(ExecTaskWithProgress.class);
	private TaskExecutor taskExecutor;
	private boolean isRunning = false;

	/**
	 * The Constructor
	 * 
	 * @param taskExecutor
	 */
	public ExecTaskWithProgress(TaskExecutor taskExecutor) {
		this.taskExecutor = taskExecutor;
	}

	/**
	 * 
	 * Override method.Runs this operation and calls the exec method in Type
	 * TaskExecutor
	 * 
	 */
	public void run(final IProgressMonitor monitor) throws InterruptedException {
		monitor.beginTask(Messages.msgRunning, IProgressMonitor.UNKNOWN);
		if (monitor.isCanceled()) {
			return;
		}

		isRunning = true;
		Thread thread = new Thread() {
			public void run() {
				while (monitor != null && !monitor.isCanceled() && isRunning) {
					try {
						sleep(50);
					} catch (InterruptedException e) {
						logger.error(e.getMessage(), e);
					}
				}
				if (monitor != null && monitor.isCanceled()) {
					isRunning = false;
					taskExecutor.cancel();
				}
			}
		};
		thread.start();
		taskExecutor.exec(monitor);
		isRunning = false;
		monitor.done();
	}

	/**
	 * Calls the exec(boolean fork,boolean cancelable)
	 * 
	 */
	public void exec() {
		this.exec(true, true);
	}

	/**
	 * Calls the run method of ProgressMonitorDialog easily
	 * 
	 * @param fork
	 * @param cancelable
	 */
	public void exec(boolean fork, boolean cancelable) {
		try {
			new ProgressMonitorDialog(null).run(fork, cancelable, this);
		} catch (InvocationTargetException e) {
			logger.error(e.getMessage(), e);
		} catch (InterruptedException e) {
			logger.error(e.getMessage(), e);
		}
	}

	/**
	 * 
	 * Calls when can not sure to the execution time,first show cursor,then show
	 * progress dialog
	 * 
	 */
	public void busyCursorWhile() {
		try {
			PlatformUI.getWorkbench().getProgressService().busyCursorWhile(this);
		} catch (InvocationTargetException e) {
			logger.error(e.getMessage(), e);
		} catch (InterruptedException e) {
			logger.error(e.getMessage(), e);
		}
	}
}
