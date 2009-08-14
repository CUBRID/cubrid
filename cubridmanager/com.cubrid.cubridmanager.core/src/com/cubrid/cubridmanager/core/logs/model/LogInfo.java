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

/**
 * 
 * Log information model class
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-4-3 created by wuyingshi
 */
public class LogInfo {

	private String path = null;
	private String type = null;
	private String owner = null;
	private String size = null;
	private String lastupdate = null;
	private String filename = null;

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
	 * get the owner.
	 * 
	 * @return
	 */
	public String getOwner() {
		return owner;
	}

	/**
	 * set the owner.
	 * 
	 * @param owner
	 */
	public void setOwner(String owner) {
		this.owner = owner;
	}

	/**
	 * get the size.
	 * 
	 * @return
	 */
	public String getSize() {
		return size;
	}

	/**
	 * set the size.
	 * 
	 * @param size
	 */
	public void setSize(String size) {
		this.size = size;
	}

	/**
	 * get the lastupdate.
	 * 
	 * @return
	 */
	public String getLastupdate() {
		return lastupdate;
	}

	/**
	 * set the lastupdate.
	 * 
	 * @param lastupdate
	 */
	public void setLastupdate(String lastupdate) {
		this.lastupdate = lastupdate;
	}

	/**
	 * get the type.
	 * 
	 * @return
	 */
	public String getType() {
		return type;
	}

	/**
	 * set the type.
	 * 
	 * @param type
	 */
	public void setType(String type) {
		this.type = type;
	}

	/**
	 * get the name of file.
	 * 
	 * @return
	 */
	public String getName() {
		if (path != null && path.lastIndexOf("/") > 0) {
			return path.substring(path.lastIndexOf("/") + 1);
		}
		return "";
	}

	/**
	 * get the filename.
	 * 
	 * @return
	 */
	public String getFilename() {
		return filename;
	}

	/**
	 * set the filename.
	 * 
	 * @param filename
	 */
	public void setFilename(String filename) {
		this.filename = filename;
	}

}
