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

public class Jobs implements Comparable {
	public String backupid;
	public String path;
	public String periodtype;
	public String perioddetail;
	public String time;
	public String level;
	public String archivedel;
	public String updatestatus;
	public String storeold;
	public String onoff;
	public String zip;
	public String check;
	public String mt;

	public Jobs(String p_backupid, String p_path, String p_periodtype,
			String p_perioddetail, String p_time, String p_level,
			String p_archivedel, String p_updatestatus, String p_storeold,
			String p_onoff, String p_zip, String p_check, String p_mt) {
		backupid = new String(p_backupid);
		path = new String(p_path);
		periodtype = new String(p_periodtype);
		perioddetail = new String(p_perioddetail);
		time = new String(p_time);
		level = new String(p_level);
		archivedel = new String(p_archivedel);
		updatestatus = new String(p_updatestatus);
		storeold = new String(p_storeold);
		onoff = new String(p_onoff);
		zip = new String(p_zip);
		check = new String(p_check);
		mt = new String(p_mt);
	}

	public static ArrayList JobsInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.JobInfo;
	}

	public int compareTo(Object obj) {
		return backupid.compareTo(((Jobs) obj).backupid);
	}
}
