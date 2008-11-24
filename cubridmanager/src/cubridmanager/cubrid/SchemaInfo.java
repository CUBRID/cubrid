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

public class SchemaInfo implements Comparable {
	public String name;
	public String type;
	public String schemaowner;
	public String virtual;
	public String ldbname;
	public String is_partitionGroup;
	public String partitionGroupName;
	public ArrayList classAttributes = null; // DBAttribute
	public ArrayList attributes = null; // DBAttribute
	public ArrayList classMethods = null; // DBMethod
	public ArrayList methods = null; // DBMethod
	public ArrayList classResolutions = null; // DBResolution
	public ArrayList resolutions = null; // DBResolution
	public ArrayList constraints = null; // Constraint
	public ArrayList superClasses = null; // String
	public ArrayList subClasses = null;
	public ArrayList OidList = null;
	public ArrayList methodFiles = null;
	public ArrayList querySpecs = null;

	public SchemaInfo(String p_name, String p_type, String p_schemaowner,
			String p_virtual) {
		name = new String(p_name);
		type = new String(p_type);
		schemaowner = new String(p_schemaowner);
		virtual = new String(p_virtual);
	}

	public static ArrayList SchemaInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.Schema;
	}

	public boolean isSystemClass() {
		if (type.equals("system"))
			return true;
		else
			return false;
	}

	public int compareTo(Object obj) {
		return name.compareTo(((SchemaInfo) obj).name);
	}
}
