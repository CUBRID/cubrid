/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package cubridmanager.cubrid;

import java.util.ArrayList;

import cubridmanager.MainRegistry;
import cubridmanager.cas.CASItem;
import cubridmanager.cubrid.AuthItem;

public class LogFileInfo implements Comparable {
	public String filename;
	public String fileowner;
	public String size;
	public String date;
	public String path;
	public String type = null;

	public LogFileInfo(String p_filename, String p_fileowner, String p_size,
			String p_date, String p_path) {
		filename = new String(p_filename);
		fileowner = new String(p_fileowner);
		size = new String(p_size);
		date = new String(p_date);
		path = new String(p_path);
	}

	public LogFileInfo(String p_type, String p_filename, String p_fileowner,
			String p_size, String p_date, String p_path) {
		type = new String(p_type);
		filename = new String(p_filename);
		fileowner = new String(p_fileowner);
		size = new String(p_size);
		date = new String(p_date);
		path = new String(p_path);
	}

	public static ArrayList DBLogInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.LogInfo;
	}

	public static ArrayList BrokerLog_get(String brkname) {
		CASItem ci = MainRegistry.CASinfo_find(brkname);
		if (ci == null)
			return null;
		return ci.loginfo;
	}

	public int compareTo(Object obj) {
		return filename.compareTo(((LogFileInfo) obj).filename);
	}
}
