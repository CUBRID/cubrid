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
package com.cubrid.cubridmanager.app;

import java.io.BufferedReader;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.UnsupportedEncodingException;
import java.net.HttpURLConnection;
import java.net.MalformedURLException;
import java.net.URL;

import com.cubrid.cubridmanager.core.common.xml.IXMLMemento;
import com.cubrid.cubridmanager.core.common.xml.XMLMemento;
import com.cubrid.cubridmanager.ui.spi.Version;

/**
 * 
 * This util class is responsible to connect some urls
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-23 created by pangqiren
 */
public class UrlConnUtil {

	public static final String checkNewInfoUrl = "http://www.cubrid.com/news.htm";
	public static final String checkNewVersionUrl = "http://www.cubrid.com/check_version.cub";

	/**
	 * 
	 * Return whether the url can be connected
	 * 
	 * @param url
	 * @return
	 */
	public static boolean isUrlExist(String url) {
		HttpURLConnection conn = null;
		try {
			conn = (HttpURLConnection) new URL(url).openConnection();
			conn.setRequestMethod("HEAD");
			if (conn.getResponseCode() == HttpURLConnection.HTTP_OK) {
				return true;
			}
		} catch (MalformedURLException ignored) {
		} catch (IOException ignored) {
		} finally {
			if (conn != null) {
				conn.disconnect();
			}
		}
		return false;
	}

	/**
	 * 
	 * Get Url Content
	 * 
	 * @param urlStr
	 * @return
	 */
	public static String getContent(String urlStr) {
		HttpURLConnection conn = null;
		BufferedReader in = null;
		try {
			URL url = new URL(urlStr);
			conn = (HttpURLConnection) url.openConnection();
			conn.setRequestProperty("Http-User-Agent", "CUBRID-MANAGER");
			in = new BufferedReader(
					new InputStreamReader(conn.getInputStream()));
			StringBuffer sb = new StringBuffer();
			String inputLine;
			while ((inputLine = in.readLine()) != null) {
				sb.append(inputLine);
				sb.append("\n");
			}
			return sb.toString();
		} catch (MalformedURLException ignored) {
		} catch (IOException ignored) {
		} finally {
			if (in != null) {
				try {
					in.close();
				} catch (IOException ignored) {
				}
			}
			if (conn != null) {
				conn.disconnect();
			}
		}
		return "";
	}

	/**
	 * 
	 * Return whether CUBRID new version exist
	 * 
	 * @return
	 */
	public static boolean isExistNewCubridVersion() {
		if (isUrlExist(checkNewVersionUrl)) {
			String content = getContent(checkNewVersionUrl);
			if (content == null || content.trim().length() <= 0) {
				return false;
			}
			content = content.toUpperCase();
			if (content.indexOf("<HTML") >= 0)
				content = content.substring(content.indexOf("<HTML"));
			ByteArrayInputStream in = null;
			try {
				in = new ByteArrayInputStream(content.getBytes("UTF-8"));
				IXMLMemento memento = XMLMemento.loadMemento(in);
				IXMLMemento[] children = memento.getChildren("BODY");
				if (children != null && children.length == 1) {
					content = children[0].getTextData();
				}
			} catch (UnsupportedEncodingException e) {
			}
			if (Version.buildId != null
					&& !Version.buildId.matches("^(\\d+\\.){3}\\d+$")) {
				return false;
			}
			String[] localBuildIdArr = Version.buildId.split("\\.");
			if (content != null && content.trim().length() > 0
					&& content.trim().matches("^(\\d+\\.){3}\\d+$")) {
				String[] latestBuildIdArr = content.trim().split("\\.");
				if (latestBuildIdArr == null) {
					return false;
				}
				for (int i = 0; i < localBuildIdArr.length
						&& i < latestBuildIdArr.length; i++) {
					int localBuildId = Integer.parseInt(localBuildIdArr[i]);
					int latestBuildId = Integer.parseInt(latestBuildIdArr[i]);
					if (latestBuildId > localBuildId) {
						return true;
					} else if (latestBuildId < localBuildId) {
						return false;
					}
				}
			}
		}
		return false;
	}
}
