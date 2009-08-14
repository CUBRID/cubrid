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
package com.cubrid.cubridmanager.core.common.xml;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import javax.xml.transform.OutputKeys;
import javax.xml.transform.Result;
import javax.xml.transform.Source;
import javax.xml.transform.Transformer;
import javax.xml.transform.TransformerFactory;
import javax.xml.transform.dom.DOMSource;
import javax.xml.transform.stream.StreamResult;

import org.apache.log4j.Logger;
import org.w3c.dom.Attr;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NamedNodeMap;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;
import org.w3c.dom.Text;
import org.xml.sax.InputSource;

import com.cubrid.cubridmanager.core.common.log.LogUtil;

/**
 * 
 * A Memento which implement IXMLMemento interface is responsible to restore
 * orginate object state from xml format file and persist memento object to xml
 * format file
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public final class XMLMemento implements
		IXMLMemento {

	private static final Logger logger = LogUtil.getLogger(XMLMemento.class);
	private Document document;
	private Element element;

	/**
	 * @see IXMLMemento#createChild(String)
	 */
	public IXMLMemento createChild(String name) {
		Element child = document.createElement(name);
		element.appendChild(child);
		return new XMLMemento(document, child);
	}

	/**
	 * @see IXMLMemento#getChild(String)
	 */
	public IXMLMemento getChild(String name) {
		NodeList nodes = element.getChildNodes();
		int size = nodes.getLength();
		if (size == 0) {
			return null;
		}
		for (int i = 0; i < size; i++) {
			Node node = nodes.item(i);
			if (node instanceof Element) {
				Element ele = (Element) node;
				if (ele.getNodeName().equals(name))
					return new XMLMemento(document, ele);
			}
		}
		return null;
	}

	/**
	 * @see IXMLMemento#getChildren(String)
	 */
	public IXMLMemento[] getChildren(String name) {
		NodeList nodes = element.getChildNodes();
		int size = nodes.getLength();
		if (size == 0) {
			return new IXMLMemento[0];
		}
		List<Element> list = new ArrayList<Element>(size);
		for (int i = 0; i < size; i++) {
			Node node = nodes.item(i);
			if (node instanceof Element) {
				Element ele = (Element) node;
				if (ele.getNodeName().equals(name))
					list.add(ele);
			}
		}
		size = list.size();
		IXMLMemento[] results = new IXMLMemento[size];
		for (int i = 0; i < size; i++) {
			results[i] = new XMLMemento(document, list.get(i));
		}
		return results;
	}

	/**
	 * @see IXMLMemento#getFloat(String)
	 */
	public Float getFloat(String key) {
		Attr attr = element.getAttributeNode(key);
		if (attr == null) {
			return null;
		}
		String strValue = attr.getValue();
		try {
			return new Float(strValue);
		} catch (NumberFormatException e) {
			return null;
		}
	}

	/**
	 * @see IXMLMemento#getBoolean(String)
	 */
	public Boolean getBoolean(String key) {
		Attr attr = element.getAttributeNode(key);
		if (attr == null) {
			return false;
		}
		String strValue = attr.getValue();
		if ("true".equalsIgnoreCase(strValue)) {
			return true;
		}
		return false;
	}

	/**
	 * @see IXMLMemento#getInteger(String)
	 */
	public Integer getInteger(String key) {
		Attr attr = element.getAttributeNode(key);
		if (attr == null) {
			return null;
		}
		String strValue = attr.getValue();
		try {
			return new Integer(strValue);
		} catch (NumberFormatException e) {
			return null;
		}
	}

	/**
	 * @see IXMLMemento#getString(String)
	 */
	public String getString(String key) {
		Attr attr = element.getAttributeNode(key);
		if (attr == null) {
			return null;
		}
		return attr.getValue();
	}

	/**
	 * @see IXMLMemento#getTextData(String)
	 */
	public String getTextData() {
		Text textNode = getTextNode();
		if (textNode == null) {
			return null;
		} else {
			return textNode.getData();
		}
	}

	/**
	 * @see IXMLMemento#getString(String)
	 */
	public List<String> getAttributeNames() {
		NamedNodeMap map = element.getAttributes();
		int size = map.getLength();
		List<String> list = new ArrayList<String>();
		for (int i = 0; i < size; i++) {
			Node node = map.item(i);
			String name = node.getNodeName();
			list.add(name);
		}
		return list;
	}

	/**
	 * @see IXMLMemento#putInteger(String,int)
	 */
	public void putInteger(String key, int n) {
		element.setAttribute(key, String.valueOf(n));
	}

	/**
	 * @see IXMLMemento#putString(String,String)
	 */
	public void putString(String key, String value) {
		if (value == null)
			return;
		element.setAttribute(key, value);
	}

	/**
	 * @see IXMLMemento#putBoolean(String, boolean)
	 */
	public void putBoolean(String key, boolean value) {
		element.setAttribute(key, value ? "true" : "false");
	}

	/**
	 * @see IXMLMemento#putTextData(String)
	 */
	public void putTextData(String data) {
		Text textNode = getTextNode();
		if (textNode == null) {
			textNode = document.createTextNode(data);
			element.insertBefore(textNode, element.getFirstChild());
		} else {
			textNode.setData(data);
		}
	}

	/**
	 * Load a memento from the given stream.
	 * 
	 * @param in java.io.InputStream
	 * @return the memento object
	 */
	public static IXMLMemento loadMemento(InputStream in) {
		return getRoot(in);
	}

	/**
	 * Load a memento from the given filename.
	 * 
	 * @param filename java.lang.String
	 * @return the memento object
	 * @exception java.io.IOException
	 */
	public static IXMLMemento loadMemento(String filename) throws IOException {
		InputStream in = null;
		try {
			in = new BufferedInputStream(new FileInputStream(filename));
			return XMLMemento.getRoot(in);
		} finally {
			try {
				if (in != null)
					in.close();
			} catch (Exception e) {
				logger.error(e);
			}
		}
	}

	/**
	 * create a root memento for writing a document.
	 * 
	 * @param name a node name
	 * @return a memento object
	 */
	public static XMLMemento createWriteRoot(String name) {
		Document document;
		try {
			document = DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument();
			Element element = document.createElement(name);
			document.appendChild(element);
			return new XMLMemento(document, element);
		} catch (ParserConfigurationException e) {
			throw new Error(e);
		}
	}

	/**
	 * Return the contents of this memento as a byte array.
	 * 
	 * @return byte[]
	 * @throws IOException if anything goes wrong
	 */
	public byte[] getContents() throws IOException {
		ByteArrayOutputStream out = new ByteArrayOutputStream();
		save(out);
		return out.toByteArray();
	}

	/**
	 * Return an input stream of this memento.
	 * 
	 * @return java.io.InputStream
	 * @throws IOException if anything goes wrong
	 */
	public InputStream getInputStream() throws IOException {
		ByteArrayOutputStream out = new ByteArrayOutputStream();
		save(out);
		return new ByteArrayInputStream(out.toByteArray());
	}

	/**
	 * Save this Memento to a stream.
	 * 
	 * @param os the ouput stream
	 * @throws IOException
	 */
	public void save(OutputStream os) throws IOException {
		Result result = new StreamResult(os);
		Source source = new DOMSource(document);
		try {
			Transformer transformer = TransformerFactory.newInstance().newTransformer();
			transformer.setOutputProperty(OutputKeys.INDENT, "yes");
			transformer.setOutputProperty(OutputKeys.METHOD, "xml");
			transformer.setOutputProperty(OutputKeys.ENCODING, "UTF-8");
			transformer.setOutputProperty(
					"{http://xml.apache.org/xalan}indent-amount", "2");
			transformer.transform(source, result);
		} catch (Exception e) {
			throw (IOException) (new IOException().initCause(e));
		}
	}

	/**
	 * Save the memento to the given file.
	 * 
	 * @param filename java.lang.String
	 * @exception java.io.IOException
	 */
	public void saveToFile(String filename) throws IOException {
		BufferedOutputStream w = null;
		try {
			w = new BufferedOutputStream(new FileOutputStream(filename));
			save(w);
		} catch (IOException e) {
			throw e;
		} catch (Exception e) {
			throw new IOException(e.getLocalizedMessage());
		} finally {
			if (w != null) {
				try {
					w.close();
				} catch (Exception e) {
					logger.error(e);
				}
			}
		}
	}

	/**
	 * 
	 * Save the memento to the given str.
	 * 
	 * @return the string content of this memento
	 * @throws java.io.IOException
	 */
	public String saveToString() throws IOException {
		ByteArrayOutputStream out = new ByteArrayOutputStream();
		save(out);
		return out.toString("UTF-8");
	}

	/**
	 * create a memento for the document and element. For simplicity you should
	 * use getRoot and createRoot to create the initial mementos on a document.
	 * 
	 * @param doc
	 * @param el
	 */
	private XMLMemento(Document doc, Element el) {
		document = doc;
		element = el;
	}

	/**
	 * Return the Text node of the memento. Each memento is allowed only one
	 * Text node.
	 * 
	 * @return the Text node of the memento, or <code>null</code> if the
	 *         memento has no Text node.
	 */
	private Text getTextNode() {
		NodeList nodes = element.getChildNodes();
		int size = nodes.getLength();
		if (size == 0) {
			return null;
		}
		for (int i = 0; i < size; i++) {
			Node node = nodes.item(i);
			if (node instanceof Text) {
				return (Text) node;
			}
		}
		return null;
	}

	/**
	 * Get memento object from xml stream
	 * 
	 * @param in
	 * @return the memento object
	 */
	private static XMLMemento getRoot(InputStream in) {
		Document document = null;
		try {
			DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();
			DocumentBuilder parser = factory.newDocumentBuilder();
			document = parser.parse(new InputSource(in));
			Node node = document.getFirstChild();
			if (node instanceof Element)
				return new XMLMemento(document, (Element) node);
		} catch (Exception e) {
			logger.error(e);
		} finally {
			try {
				in.close();
			} catch (Exception e) {
				logger.error(e);
			}
		}
		return null;
	}
}