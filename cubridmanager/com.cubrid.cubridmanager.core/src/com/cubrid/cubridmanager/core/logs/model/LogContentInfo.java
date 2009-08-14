/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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

package com.cubrid.cubridmanager.core.logs.model;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.common.model.IModel;

/**
 * 
 * This class is responsible to store log content.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-4-3 created by wuyingshi
 */
public class LogContentInfo implements IModel {

	private String path;
	private String start;
	private String end;
	private String total;
	private List<String> lineList;

	/**
	 * get task name.
	 * 
	 * @return
	 */
	public String getTaskName() {
		return "viewlog";
	}

	/**
	 * get the path.
	 * 
	 * @return
	 */
	public String getPath() {
		return path;
	}

	/**
	 * set the path.
	 * 
	 * @param path
	 */

	public void setPath(String path) {
		this.path = path;
	}

	/**
	 * get the start value.
	 * 
	 * @return
	 */

	public String getStart() {
		return start;
	}

	/**
	 * set the start value.
	 * 
	 * @param start
	 */
	public void setStart(String start) {
		this.start = start;
	}

	/**
	 * get the end value.
	 * 
	 * @return
	 */

	public String getEnd() {
		return end;
	}

	/**
	 * set the end value.
	 * 
	 * @param end
	 */
	public void setEnd(String end) {
		this.end = end;
	}

	/**
	 * get the total.
	 * 
	 * @return
	 */
	public String getTotal() {
		return total;
	}

	/**
	 * set the total.
	 * 
	 * @param total
	 */
	public void setTotal(String total) {
		this.total = total;
	}

	/**
	 * get the lineList.
	 * 
	 * @return
	 */
	public List<String> getLine() {
		return lineList;
	}

	/**
	 * add str to the lineList.
	 * 
	 * @param str
	 */
	public void addLine(String str) {
		if (this.lineList == null) {
			lineList = new ArrayList<String>();
		}
		lineList.add(str);
	}

}
