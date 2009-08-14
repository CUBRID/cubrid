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
package com.cubrid.cubridmanager.core.cubrid.dbspace.model;

public class DbSpaceInfo {

	private String spacename = null;

	/*
	 * GENERIC,DATA,TEMP,INDEX,Active_log,Archive_log
	 */
	private String type = null;

	private String location = null;

	private int totalpage = 0;

	private int freepage = 0;

	private String date = null;

	private int volumeCount=0;
	
	private String totalPageStr=null;
	private String totalSizeStr=null;
	
	public String getTotalPageStr() {
    	return totalPageStr;
    }

	public void setTotalPageStr(String totalPageStr) {
    	this.totalPageStr = totalPageStr;
    }

	public String getTotalSizeStr() {
    	return totalSizeStr;
    }

	public void setTotalSizeStr(String totalSizeStr) {
    	this.totalSizeStr = totalSizeStr;
    }

	public String getSpacename() {
		return spacename;
	}

	public void setSpacename(String spacename) {
		this.spacename = spacename;
	}

	public String getType() {
		return type;
	}

	public void setType(String type) {
		this.type = type;
	}

	public String getLocation() {
		return location;
	}

	public void setLocation(String location) {
		this.location = location;
	}

	public int getTotalpage() {
		return totalpage;
	}

	public void setTotalpage(int totalpage) {
		this.totalpage = totalpage;
	}

	public int getFreepage() {
		return freepage;
	}

	public void setFreepage(int freepage) {
		this.freepage = freepage;
	}

	public String getDate() {
		return date;
	}

	public void setDate(String date) {
		this.date = date;
	}

	public int getVolumeCount() {
    	return volumeCount;
    }

	public void plusVolumeCount() {
    	this.volumeCount++;
    }

}
