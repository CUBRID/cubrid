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

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.core.runtime.jobs.Job;

import com.cubrid.cubridmanager.ui.spi.Messages;

/**
 * 
 * An operation approver that implements the Type Job and executes the task by
 * Type <link>#TaskJobExecutor</link>.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class TaskJob extends
		Job {
	private TaskJobExecutor taskJobExecutor = null;
	private boolean isRunning = false;

	/**
	 * @param name
	 */
	public TaskJob(String name, TaskJobExecutor taskJobExecutor) {
		super(name);
		this.taskJobExecutor = taskJobExecutor;
	}

	@Override
	protected IStatus run(final IProgressMonitor monitor) {
		monitor.beginTask(Messages.msgRunning, IProgressMonitor.UNKNOWN);
		if (monitor.isCanceled()) {
			return Status.CANCEL_STATUS;
		}

		isRunning = true;
		Thread thread = new Thread() {
			public void run() {
				while (monitor != null && !monitor.isCanceled() && isRunning) {
					try {
						sleep(500);
					} catch (InterruptedException e) {
					}
				}
				if (monitor != null && monitor.isCanceled()) {
					isRunning = false;
					taskJobExecutor.cancel();
				}
			}
		};
		thread.start();
		IStatus status = taskJobExecutor.exec(monitor);
		isRunning = false;
		monitor.done();
		return status;
	}
}
