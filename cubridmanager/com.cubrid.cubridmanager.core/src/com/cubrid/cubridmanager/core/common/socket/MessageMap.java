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
package com.cubrid.cubridmanager.core.common.socket;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * 
 * This class is responsible to store sent or response message information;
 * when it is sent message information,the sent message is ordered by order
 * array. when it is response message information,the orders array is useless.
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-4 created by moulinwang
 */
public class MessageMap {
	private List<String> infoStoreList = new ArrayList<String>(10);
	private String[] orders = null;
	private String[][] groups = null;

	/**
	 * When it store response message,this constructor will be used for
	 * TreeNode
	 */
	public MessageMap() {
	}

	/**
	 * When it store sent message,this constructor will be used,the sent
	 * message is order
	 */
	public MessageMap(String[] orders) {
		parse(orders);
	}

	/**
	 * Search the order of the string in orders array if not found, return -1
	 * 
	 * @param key String
	 * @return
	 */
	private int getLocation(String key) {
		if (orders != null) {
			for (int i = 0; i < orders.length; i++) {
				if (key.equals(orders[i])) {
					return i;
				}
			}
		}
		return -1;
	}

	/**
	 * Get the first value of the key string
	 * 
	 * @param key
	 * @return
	 */
	public String getValue(String key) {
		for (String str : infoStoreList) {
			int index = str.indexOf(":");
			String strkey = str.substring(0, index);
			if (strkey.equals(key)) {
				return str.substring(index + 1);
			}
		}
		return null;
	}

	/**
	 * Get all values of the key string
	 * 
	 * @param key
	 * @return
	 */
	public String[] getValues(String key) {
		List<String> list = new ArrayList<String>();
		for (String str : infoStoreList) {
			int index = str.indexOf(":");
			String strkey = str.substring(0, index);
			if (strkey.equals(key)) {
				list.add(str.substring(index + 1));
			}
		}
		int size = list.size();
		if (size == 0) {
			return null;
		} else {
			String[] retValues = list.toArray(new String[size]);
			return retValues;
		}
	}

	/**
	 * get all value by Map interface
	 * 
	 * @return map
	 */
	public Map<String, String> getValueByMap() {
		Map<String, String> map = new TreeMap<String, String>();
		for (String str : infoStoreList) {
			String[] vals = str.split(":", 2);
			if (vals.length == 2 && !vals[0].equals("open")
					&& !vals[0].equals("close"))
				map.put(vals[0], vals[1]);
		}
		return map;
	}

	/**
	 * Insert new message item into map
	 * 
	 * @param str
	 */
	public void add(String str) {
		int index = str.indexOf(":");
		assert (index > 0);
		String key = str.substring(0, index);
		String value = str.substring(index + 1);
		add(key, value);
	}

	/**
	 * Add key_value pair into map by the order defined by orders array permit
	 * multi values
	 * 
	 * @param key
	 * @param value
	 */
	public void add(String key, String value) {
		int loc = getLocation(key);
		if (loc == -1) {
			infoStoreList.add(key + ":" + value);
			return;
		} else {
			for (int i = 0; i < infoStoreList.size(); i++) {
				String str = infoStoreList.get(i);
				int index = str.indexOf(":");
				String strkey = str.substring(0, index);
				int strloc = getLocation(strkey);
				// insert before the first larger location, else insert to the
				// end
				if (strloc > loc) {
					infoStoreList.add(i, key + ":" + value);
					return;
				} else if (strloc == loc) {
					continue;
				}
			}
		}
		infoStoreList.add(key + ":" + value);
	}

	/**
	 * Get key value of the below string format(key:value)
	 * 
	 * @param str
	 * @return
	 */
	private String getKey(String str) {
		int index = str.indexOf(":");
		assert (index > 0);
		return str.substring(0, index);
	}

	/**
	 * Add or modify the value of this key;if this key exist,it will modify it's
	 * value;or add the value of this key.
	 * 
	 * @param key
	 * @param value
	 */
	public void addOrModifyValue(String key, String value) {
		// remove old item
		if (value == null) {
			return;
		}
		for (int i = infoStoreList.size() - 1; i >= 0; i--) {
			String tmpkey = getKey(infoStoreList.get(i));
			if (tmpkey.equals(key)) {
				infoStoreList.remove(i);
				infoStoreList.add(i, key + ":" + value);
				return;
			}
		}
		add(key, value);
	}

	/**
	 * Add or modify the values of this key;if this key exist,it will modify
	 * it's values;or add the values of this key.
	 * 
	 * @param key
	 * @param values
	 */
	public void addOrModifyValues(String key, String[] values) {
		// remove old item
		for (int i = infoStoreList.size() - 1; i >= 0; i--) {
			String tmpkey = getKey(infoStoreList.get(i));
			if (tmpkey.equals(key)) {
				infoStoreList.remove(i);
			}
		}
		if (values == null || values.length == 0) {
			return;
		}
		for (String value : values) {
			add(key, value);
		}
	}

	private String[] getGroupByKey(String key) {
		if (this.groups == null)
			return null;
		for (String[] group : groups) {
			for (String groupkey : group) {
				if (groupkey.equals(key)) {
					return group;
				}
			}
		}
		return null;
	}

	private String getGroupString(String[] group) {
		assert (group != null && group.length > 0);
		List<String[]> list = new ArrayList<String[]>();
		StringBuffer bf = new StringBuffer();
		int length = getValues(group[0]).length;
		for (String key : group) {
			String[] values = getValues(key);
			list.add(values);
		}
		for (int i = 0; i < length; i++) {
			for (int j = 0; j < group.length; j++) {
				String key = group[j];
				String[] values = list.get(j);
				if (values != null && values.length > i) {
					String value = values[i];
					bf.append(key + ":" + value + "\n");
				}
			}
		}
		return bf.toString();
	}

	/**
	 * Get sent or response message String
	 */
	public String toString() {
		List<String[]> list = new ArrayList<String[]>();
		StringBuffer bf = new StringBuffer();
		for (String str : infoStoreList) {
			String key = getKey(str);
			String[] group = getGroupByKey(key);
			if (null != group) {
				int index = list.indexOf(group);
				if (-1 != index) {
					// do nothing
				} else {
					bf.append(getGroupString(group));
					list.add(group);
				}
			} else {
				bf.append(str).append("\n");
			}
		}
		return bf.append("\n").toString();
	}

	/**
	 * Get sent message order
	 * 
	 * @return
	 */
	public String[] getOrders() {
		return orders;
	}

	/**
	 * Set sent message order
	 * 
	 * @param orders
	 */
	public void setOrders(String[] orders) {
		parse(orders);
	}

	/**
	 * 
	 * Clear message information
	 * 
	 */
	public void clear() {
		infoStoreList.clear();
	}

	/**
	 * 
	 * Parse ordered message keys
	 * 
	 * @param orders
	 */
	private void parse(String[] orders) {
		if (orders == null || orders.length <= 0) {
			return;
		}
		List<String> orderList = new ArrayList<String>();
		List<String[]> groupList = new ArrayList<String[]>();
		for (int i = 0; i < orders.length; i++) {
			String str = orders[i];
			if (str.indexOf("{") == 0
					&& str.lastIndexOf("}") == str.length() - 1) {
				String[] strArr = str.replace("{", "").replace("}", "").split(
						",");
				if (strArr != null && strArr.length > 0) {
					groupList.add(strArr);
					for (int j = 0; j < strArr.length; j++)
						orderList.add(strArr[j]);
				}
			} else {
				orderList.add(str);
			}
		}
		this.orders = new String[orderList.size()];
		this.orders = orderList.toArray(this.orders);
		if (groupList.size() > 0) {
			this.groups = new String[groupList.size()][];
			for (int i = 0; i < groupList.size(); i++) {
				this.groups[i] = groupList.get(i);
			}
		}
	}

	/**
	 * 
	 * Get reponsed messages
	 * 
	 * @return
	 */
	public List<String> getResponseMessage() {
		return infoStoreList;
	}
}