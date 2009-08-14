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

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.List;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.ui.query.StructQueryPlan;
import com.cubrid.cubridmanager.ui.spi.CommonTool;

/**
 * 
 * Plan history file manager
 * 
 * PlanHistoryManager Description
 * 
 * @author pcraft
 * @version 1.0 - 2009. 06. 06 created by pcraft
 */
public class PlanHistoryManager {

	private static final Logger logger = LogUtil.getLogger(PlanHistoryManager.class);

	public static List<StructQueryPlan> openFile(File file) throws IOException {
		String xml = read(file);
		if (xml == null || xml.length() == 0) {
			return null;
		}
		logger.debug(xml);
		List<StructQueryPlan> sqList = StructQueryPlan.unserialize(xml);
		logger.debug(sqList);

		return sqList;
	}

	public static void saveFile(File file, List<StructQueryPlan> sqList) throws IOException {
		write(file, StructQueryPlan.serialize(sqList));
	}
	
	private static String read(File file) throws IOException {
		BufferedReader br = null;
		try {

			br = new BufferedReader(new FileReader(file));
			StringBuilder buff = new StringBuilder();
			String line = br.readLine();
			while (line != null) {
				buff.append(line).append(CommonTool.getLineSeparator());
				line = br.readLine();
			}

			return buff.toString();
		} finally {
			try {
				br.close();
			} catch (Exception e) {
				logger.error(e.getMessage(), e);
			}
		}
	}

	public static boolean write(File file, String xml) throws IOException {
		if (file == null)
			return false;

		BufferedWriter bw = null;
		try {
			bw = new BufferedWriter(new FileWriter(file));
			bw.write(xml);

			return true;
		} finally {
			try {
				bw.close();
			} catch (Exception e) {
				logger.error(e.getMessage(), e);
			}
		}
	}

}
