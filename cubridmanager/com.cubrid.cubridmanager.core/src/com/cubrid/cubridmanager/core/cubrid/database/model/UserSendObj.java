package com.cubrid.cubridmanager.core.cubrid.database.model;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class UserSendObj {

	String dbname;

	String username;

	String userpass;

	List<String> groups;

	List<String> addmembers;

	List<String> removemembers;

	Map<String, String> authorization;

	public String getDbname() {
		return dbname;
	}

	public void setDbname(String dbname) {
		this.dbname = dbname;
	}

	public String getUsername() {
		return username;
	}

	public void setUsername(String username) {
		this.username = username;
	}

	public String getUserpass() {
		return userpass;
	}

	public void setUserpass(String userpass) {
		this.userpass = userpass;
	}

	public List<String> getGroups() {
		if(groups==null)
			return new ArrayList<String>();
		return groups;
	}

	public void addGroups(String group) {

		if (groups == null)
			groups = new ArrayList<String>();
		this.groups.add(group);
	}

	public List<String> getAddmembers() {
		if(addmembers==null)
			return new ArrayList<String>();
		return addmembers;
	}

	public void addAddmembers(String member) {
		if (addmembers == null)
			addmembers = new ArrayList<String>();
		this.addmembers.add(member);
	}

	public List<String> getRemovemembers() {
		if(removemembers==null)
			return new ArrayList<String>();
		return removemembers;
	}

	public void addRemovemembers(String member) {

		if (removemembers == null)
			removemembers = new ArrayList<String>();
		this.removemembers.add(member);
	}

	public Map<String, String> getAuthorization() {
		if (authorization == null)
			return new TreeMap<String, String>();
		return authorization;
	}

	public void addAuthorization(String key, String value) {
		if (authorization == null)
			authorization = new TreeMap<String, String>();
		this.authorization.put(key, value);
	}

}
