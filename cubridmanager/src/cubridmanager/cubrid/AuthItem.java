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

public class AuthItem implements Comparable {
	public boolean lock = false;
	public String dbname = null;
	public String dbuser = null;
	public String dbdir = null;
	public byte status = 0;
	public boolean setinfo = false;
	public int pagesize = 0;
	public double freespace = 0.0;
	public ArrayList Volinfo = new ArrayList();
	public ArrayList UserInfo = new ArrayList();
	public ArrayList Schema = new ArrayList();
	public ArrayList JobInfo = new ArrayList();
	public ArrayList AutoQueryInfo = new ArrayList();
	public ArrayList TriggerInfo = new ArrayList();
	public ArrayList LDBInfo = new ArrayList();
	public ArrayList LogInfo = new ArrayList();
	public ArrayList ParaInfo = new ArrayList();
	public boolean isDBAGroup = false;

	public AuthItem(String name, String user, byte stat) {
		dbname = new String(name);
		dbuser = new String(user);
		dbdir = new String("");
		status = stat;
		lock = false;
	}

	public AuthItem(String name, String user, String dir, byte stat) {
		dbname = new String(name);
		dbuser = new String(user);
		dbdir = new String(dir);
		status = stat;
		lock = false;
	}

	public AuthItem(String name, String user, String dir, byte stat,
			boolean dbaGroup) {
		this(name, user, dir, stat);
		isDBAGroup = dbaGroup;
	}

	public int compareTo(Object obj) {
		return dbname.compareTo(((AuthItem) obj).dbname);
	}
}
