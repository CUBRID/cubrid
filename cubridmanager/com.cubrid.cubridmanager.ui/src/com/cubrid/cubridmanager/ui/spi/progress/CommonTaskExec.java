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
import org.eclipse.swt.widgets.Display;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.ui.spi.CommonTool;

/**
 * 
 * A common type which extends the type TaakExecutor and overrides the method
 * exec.Generally ,it can be used in an action or other type of which there is
 * no dialog
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-4-16 created by lizhiqiang
 */
public class CommonTaskExec extends
		TaskExecutor {

	/**
	 * Override method
	 * 
	 * @param monitor
	 * @return
	 */

	public boolean exec(final IProgressMonitor monitor) {
		Display display = Display.getDefault();
		if (monitor.isCanceled()) {
			success = false;
			cancel();
			return success;
		}

		for (ITask task : taskList) {
			task.execute();
			final String msg = task.getErrorMsg();
			if (monitor.isCanceled()) {
				success = false;
				cancel();
				return success;
			}
			if (msg != null && msg.length() > 0 && !monitor.isCanceled()) {
				display.syncExec(new Runnable() {
					public void run() {
						CommonTool.openErrorBox(msg);
					}
				});
				success = false;
				cancel();
				return success;
			}
			if (monitor.isCanceled()) {
				success = false;
				cancel();
				return success;
			}
		}
		success = true;
		return success;
	}
	
}