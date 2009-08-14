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
package com.cubrid.cubridmanager.ui.query.control;

import java.io.IOException;
import java.util.List;
import java.util.Map;

import jxl.write.WriteException;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Table;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.dialog.Export;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * a task executor for export function
 * 
 * @author wangsl 2009-6-22
 */
public class ExportTask extends
		TaskExecutor {

	private Export export;

	private static final Logger logger = LogUtil.getLogger(ExportTask.class);

	private boolean stopped = false;

	public ExportTask(Export exp) {
		this.export = exp;
	}

	public ExportTask(List<ColumnInfo> allColumnList,
			List<Map<String, String>> allDataList, boolean doesGetOidInfo,
			CubridDatabase database) {
		this.export = new Export(allColumnList, allDataList, doesGetOidInfo,
				database);
	}

	public ExportTask(Table tblResult, boolean isSelection,
			boolean doesGetOidInfo, CubridDatabase database) {
		this.export = new Export(tblResult, isSelection, doesGetOidInfo,
				database);
	}

	@Override
	public boolean exec(final IProgressMonitor monitor) {
		if (export.isCanceled()) {
			return false;
		}
		Runnable t = new Runnable() {
			public void run() {
				while (!stopped) {
					if (monitor.isCanceled()) {
						try {
							export.cancel();
						} catch (final WriteException e) {
							logger.error(e);
							Display.getDefault().syncExec(new Runnable() {
								public void run() {
									CommonTool.openErrorBox(e.getMessage());
								}

							});
						} catch (final IOException e) {
							logger.error(e);
							Display.getDefault().syncExec(new Runnable() {

								public void run() {
									CommonTool.openErrorBox(e.getMessage());
								}

							});
						}
						stopped = true;
					}
					try {
						Thread.sleep(50);
					} catch (final InterruptedException e) {
						logger.error(e);
						Display.getDefault().syncExec(new Runnable() {

							public void run() {
								CommonTool.openErrorBox(e.getMessage());
							}

						});
					}
				}
			}

		};
		Thread thread = new Thread(t);
		thread.start();
		try {
			export.export();
			stopped = true;
		} catch (final Exception e) {
			logger.error(e);
			Display.getDefault().syncExec(new Runnable() {

				public void run() {
					CommonTool.openErrorBox(e.getMessage());
				}

			});
			return false;
		}
		if (export.getSuccessMsg() != null) {
			Display.getDefault().syncExec(new Runnable() {

				public void run() {
					CommonTool.openInformationBox(Messages.export,
							export.getSuccessMsg());
				}

			});
		}

		return true;
	}

	@Override
	public void cancel() {
		if (this.export != null) {
			try {
				this.export.cancel();
			} catch (WriteException e) {
				logger.error(e);
			} catch (IOException e) {
				logger.error(e);
			}
		}
	}

	public boolean isCancel() {
		return this.export != null ? this.export.isCanceled() : false;
	}
}
