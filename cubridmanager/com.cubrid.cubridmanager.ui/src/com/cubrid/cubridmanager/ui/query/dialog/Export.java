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

package com.cubrid.cubridmanager.ui.query.dialog;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.UnsupportedEncodingException;
import java.math.BigDecimal;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import jxl.Workbook;
import jxl.write.Label;
import jxl.write.Number;
import jxl.write.WritableSheet;
import jxl.write.WritableWorkbook;
import jxl.write.WriteException;
import jxl.write.biff.RowsExceededException;

import org.apache.log4j.Logger;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableItem;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.query.QueryOptions;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.control.ColumnInfo;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * 
 * Export data to xls or csv format file
 * 
 * @author wangsl
 * @version 1.0 - 2009-6-4 created by wangsl
 */
public class Export {

	private static final Logger logger = LogUtil.getLogger(Export.class);
	private static final String NEW_LINE = System.getProperty("line.separator");
	private String fileName;
	private File file;
	private CubridDatabase database;
	private List<ColumnInfo> columnList;
	private List<Map<String, String>> data;
	private boolean isCancel;
	private String successMsg;
	private WritableWorkbook workbook;
	private BufferedWriter csvWriter;
	private final static String dataNullValueFlag = "NULL";

	/**
	 * The constructor
	 * 
	 * @param tblResult
	 * @param b
	 * @param hasOid
	 * @param database
	 */
	public Export(Table tbl,boolean isSelection,boolean hasOid,CubridDatabase database) {
		this.database = database;
		fileName = getFileName(new String[]
			{
			        "*.xls",
			        "*.csv"
			}, new String[]
			{
			        "Excel(xls)",
			        "Comma separated value(csv)"
			});
		if (fileName == null || fileName.trim().length() == 0) {
			isCancel = true;
			return;
		}
		int colCount, itemCount;
		colCount = tbl.getColumnCount();
		TableItem[] items;
		if (isSelection) {
			itemCount = tbl.getSelectionCount();
			items = tbl.getSelection();
		} else {
			itemCount = tbl.getItemCount();
			items = tbl.getItems();
		}
		columnList = new ArrayList<ColumnInfo>();
		data = new ArrayList<Map<String, String>>();
		int start = 1;
		if (hasOid) {
			start++;
		}
		for (int j = start; j < colCount; j++) {
			ColumnInfo columnInfo = (ColumnInfo) tbl.getColumns()[j].getData();
			columnList.add(columnInfo);
		}
		for (int i = 0; i < itemCount; i++) {
			start = 1;
			if (hasOid) {
				start++;
			}
			Map<String, String> value = new HashMap<String, String>();
			for (int j = start; j < colCount; j++) {
				ColumnInfo columnInfo = (ColumnInfo) tbl.getColumns()[j].getData();
				if (dataNullValueFlag.equals(items[i].getData(j + "")))
					value.put(columnInfo.getIndex(), null);
				else
					value.put(columnInfo.getIndex(), items[i].getText(j));
			}
			data.add(value);
		}
	}

	/**
	 * export data as pointed file type
	 * 
	 * @throws FileNotFoundException
	 * @throws UnsupportedEncodingException
	 * @throws IOException
	 * @throws RowsExceededException
	 * @throws NumberFormatException
	 * @throws WriteException
	 */
	public void export() throws FileNotFoundException, UnsupportedEncodingException, IOException,
	        RowsExceededException, NumberFormatException, WriteException {
		if (fileName == null || fileName.trim().length() == 0) {
			cancel();
			return;
		}
		if (fileName.toLowerCase().endsWith(".xls")) {
			exportXls();
		} else {
			exportCsv();
		}
	}

	/**
	 * The constructor
	 * 
	 * @param columnList
	 * @param dataList
	 * @param hasOid
	 * @param database
	 */
	public Export(List<ColumnInfo> columnInfoList,List<Map<String, String>> dataList,boolean hasOid,
	        CubridDatabase database) {
		this.database = database;
		fileName = getFileName(new String[]
			{
			        "*.xls",
			        "*.csv"
			}, new String[]
			{
			        "Excel(xls)",
			        "Comma separated value(csv)"
			});
		if (fileName == null || fileName.trim().length() == 0) {
			isCancel = true;
			return;
		}
		this.columnList = new ArrayList<ColumnInfo>();
		this.data = new ArrayList<Map<String, String>>();

		int colCount = columnInfoList.size();
		int itemCount = dataList.size();
		int start = 0;
		if (hasOid) {
			start++;
		}
		for (int j = start; j < colCount; j++) {
			ColumnInfo columnInfo = columnInfoList.get(j);
			this.columnList.add(columnInfo);
		}
		for (int i = 0; i < itemCount; i++) {
			start = 0;
			if (hasOid) {
				start++;
			}
			Map<String, String> value = new HashMap<String, String>();
			for (int j = start; j < colCount; j++) {
				ColumnInfo columnInfo = columnInfoList.get(j);
				value.put(columnInfo.getIndex(), dataList.get(i).get((hasOid ? j : j + 1) + ""));
			}
			data.add(value);
		}
	}

	/**
	 * 
	 * Get file name
	 * 
	 * @param filterExts
	 * @param filterNames
	 * @return
	 */
	private String getFileName(String[] filterExts, String[] filterNames) {
		FileDialog dialog = new FileDialog(Display.getDefault().getActiveShell(), SWT.SAVE | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(filterExts);
		dialog.setFilterNames(filterNames);
		File curdir = new File(".");
		try {
			dialog.setFilterPath(curdir.getCanonicalPath());
		} catch (Exception e) {
			dialog.setFilterPath(".");
		}
		boolean doExit = false;
		while (!doExit) {
			String result = dialog.open();
			if (result != null) {
				if (!result.toLowerCase().endsWith(".xls") && !result.toLowerCase().endsWith(".csv")
				        && !result.toLowerCase().endsWith(".sql") && !result.toLowerCase().endsWith(".obs"))
					result = result.concat(".csv");
				File tmpFile = new File(result);
				if (tmpFile.exists()) {
					boolean state = CommonTool.openConfirmBox("\"" + tmpFile.getAbsolutePath() + "\"" + NEW_LINE
					        + Messages.overWrite);
					if (state) {
						new File(result + ".bak").delete();
						tmpFile.renameTo(new File(result + ".bak"));
					} else {
						doExit = false;
						continue;
					}
				}
				file = new File(result);
				return file.getName();
			} else
				doExit = true;
		}
		return null;
	}

	/**
	 * export all data in Query Editor result table cache as xls
	 * 
	 * @param columnList
	 * @param data
	 * @throws WriteException
	 * @throws NumberFormatException
	 * @throws RowsExceededException
	 */
	private void exportXls() throws IOException, FileNotFoundException, UnsupportedEncodingException,
	        RowsExceededException, NumberFormatException, WriteException {
		workbook = null;
		try {
			int sheetNum = 0;
			workbook = Workbook.createWorkbook(file);
			WritableSheet sheet = workbook.createSheet("Sheet " + sheetNum, sheetNum);
			int rowLimit = 65536; // 65536: limit xls row number.
			int columnLimit = 256; // 256: limit xls column number..

			StringBuffer addMsg = new StringBuffer("");

			int colCount = columnList.size();
			int itemCount = data.size();

			if (colCount > columnLimit) {
				if (!CommonTool.openConfirmBox(Messages.columnCountOver))
					return;
				colCount = columnLimit;
				addMsg.append(NEW_LINE);
				addMsg.append(NEW_LINE);
				addMsg.append(Messages.columnCountLimit);
			}

			for (int i = 0, xlsRecordNum = 0; i < itemCount; i++) {
				int start = 0;
				for (int j = start; j < colCount; j++) {
					String colType = columnList.get(j).getType();
					String colIndex = columnList.get(j).getIndex();
					String value = data.get(i).get(colIndex);
					if (colType.equals("INTEGER") || colType.equals("TINYINT") || colType.equals("SMALLINT")
					        || colType.equals("BIGINT")) {
						if (!value.equals(QueryOptions.STR_NULL))
							sheet.addCell(new Number(j - start, xlsRecordNum, Integer.parseInt(value)));
					} else if (colType.equals("DOUBLE") || colType.equals("FLOAT") || colType.equals("REAL")) {
						if (!value.equals(QueryOptions.STR_NULL))
							sheet.addCell(new Number(j - start, xlsRecordNum, Double.parseDouble(value)));
					} else if (colType.equals("NUMERIC") || colType.equals("DECIMAL") || colType.equals("MONETORY")) {
						if (!value.equals(QueryOptions.STR_NULL))
							sheet.addCell(new Number(j - start, xlsRecordNum, (new BigDecimal(value)).doubleValue()));
					} else {
						if (!value.equals(QueryOptions.STR_NULL))
							sheet.addCell(new Label(j - start, xlsRecordNum, value.toString()));
					}
				}
				xlsRecordNum++;

				if (((i + 1) % rowLimit) == 0 && (i + 1) < itemCount) {
					sheetNum++;
					xlsRecordNum -= rowLimit;
					sheet = workbook.createSheet("Sheet " + sheetNum, sheetNum);
				}
			}
			workbook.write();
			if (sheetNum > 0) {
				addMsg.append(NEW_LINE);
				addMsg.append(NEW_LINE);
				addMsg.append(Messages.bind(Messages.exportLimit, rowLimit));
				addMsg.append(NEW_LINE);
				addMsg.append(sheetNum + 1 + " ");
				addMsg.append(Messages.sheetCreated);
			}

			successMsg = Messages.bind(Messages.exportOk, itemCount) + addMsg;
		} finally {
			if (workbook != null) {
				workbook.close();
			}
		}
	}

	/**
	 * export all data in Query Editor result table cache as csv
	 * 
	 * @param columnList
	 * @param dataList
	 * @throws IOException
	 */
	private void exportCsv() throws IOException {
		csvWriter = null;
		try {
			String charset = QueryOptions.getCharset(database.getDatabaseInfo());
			if (charset != null && charset.trim().length() > 0) {
				csvWriter = new BufferedWriter(new OutputStreamWriter(new FileOutputStream(file), charset.trim()));
			} else {
				csvWriter = new BufferedWriter(new OutputStreamWriter(new FileOutputStream(file)));
			}

			for (int i = 0; i < data.size(); i++) {
				for (int j = 0; j < columnList.size(); j++) {
					String colType = columnList.get(j).getType();
					String colIndex = columnList.get(j).getIndex();
					if (colType.equals("MONETARY") || colType.equals("INTEGER") || colType.equals("TINYINT")
					        || colType.equals("SMALLINT") || colType.equals("BIGINT") || colType.equals("DOUBLE")
					        || colType.equals("FLOAT") || colType.equals("REAL") || colType.equals("NUMERIC")
					        || colType.equals("DECIMAL")) {
						csvWriter.write(data.get(i).get(colIndex));
					}

					else if (data.get(i).get(colIndex)==null){
						csvWriter.write(dataNullValueFlag);
					}else if ("".equals(data.get(i).get(colIndex))){
						
					}else {
						csvWriter.write(("\"" + data.get(i).get(colIndex).replaceAll("\"", "\"\"") + "\""));
					}
					if (j != columnList.size() - 1) {
						csvWriter.write(',');
					}
				}
				csvWriter.write('\n');
				csvWriter.flush();
			}
			successMsg = Messages.bind(Messages.exportOk, data.size());
		} finally {
			try {
				if (csvWriter != null)
					csvWriter.close();
			} catch (IOException e) {
				logger.error(e);
			}
		}
	}

	public void cancel() throws WriteException, IOException {
		isCancel = true;
		if (workbook != null) {
			workbook.close();
		}
		if (csvWriter != null) {
			csvWriter.close();
		}
		if (file != null) {
			file.delete();
		}
	}

	public boolean isCanceled() {
		return isCancel;
	}

	public String getSuccessMsg() {
		return successMsg;
	}

}
