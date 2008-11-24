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

public class UserInfo implements Comparable {
	public String userName;
	public String id;
	public String password;
	public boolean isGroup;
	public ArrayList groupNames; // String
	public ArrayList memberNames;
	public ArrayList groups; // UserInfo
	public ArrayList members;
	public ArrayList authorizations; // Authorizations

	public UserInfo(String p_Name, String p_id, String p_password,
			boolean p_isGroup) {
		userName = new String(p_Name);
		id = new String(p_id);
		password = new String(p_password);
		isGroup = p_isGroup;
		groupNames = new ArrayList();
		memberNames = new ArrayList();
		groups = new ArrayList();
		members = new ArrayList();
		authorizations = new ArrayList();
	}

	public static UserInfo UserInfo_find(ArrayList ar, String user) {
		for (int i = 0, n = ar.size(); i < n; i++) {
			UserInfo ui = (UserInfo) ar.get(i);
			if (ui.userName.equals(user))
				return ui;
		}
		return null;
	}

	public static ArrayList UserInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.UserInfo;
	}

	public int compareTo(Object obj) {
		return userName.compareTo(((UserInfo) obj).userName);
	}
}
