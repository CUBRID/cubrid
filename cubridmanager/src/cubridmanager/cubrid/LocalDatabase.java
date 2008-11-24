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

public class LocalDatabase implements Comparable {
	public String Name;
	public String NameIn_Host;
	public String Type;
	public String Host;
	public String U_ID;
	public String MaxActive;
	public String MinActive;
	public String DecayConstant;
	public String Directory;
	public String ObjectID;

	public LocalDatabase(String p_Name, String p_NameIn_Host, String p_Type,
			String p_Host, String p_U_ID, String p_MaxActive,
			String p_MinActive, String p_DecayConstant, String p_Directory,
			String p_ObjectID) {
		Name = new String(p_Name);
		NameIn_Host = new String(p_NameIn_Host);
		Type = new String(p_Type);
		Host = new String(p_Host);
		U_ID = new String(p_U_ID);
		MaxActive = new String(p_MaxActive);
		MinActive = new String(p_MinActive);
		DecayConstant = new String(p_DecayConstant);
		Directory = new String(p_Directory);
		ObjectID = new String(p_ObjectID);
	}

	public static ArrayList LocalDatabaseInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.LDBInfo;
	}

	public int compareTo(Object obj) {
		return Name.compareTo(((LocalDatabase) obj).Name);
	}
}
