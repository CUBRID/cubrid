package com.cubrid.cubridmanager.core.cubrid.table;

/**
 Copyright 2005 Bytecode Pty Ltd.

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

import java.io.BufferedReader;
import java.io.IOException;
import java.io.Reader;
import java.util.ArrayList;
import java.util.List;

/**
 * A very simple CSV reader released under a commercial-friendly license.
 * 
 * @author Glen Smith
 * 
 */
public class CSVReader {

	private BufferedReader br;

	private boolean hasNext = true;

	private char separator;

	private char quotechar;

	private int skipLines;

	private boolean linesSkiped;

	/** The default separator to use if none is supplied to the constructor. */
	public static final char DEFAULT_SEPARATOR = ',';

	/**
	 * The default quote character to use if none is supplied to the
	 * constructor.
	 */
	public static final char DEFAULT_QUOTE_CHARACTER = '"';

	/**
	 * The default line to start reading.
	 */
	public static final int DEFAULT_SKIP_LINES = 0;
	public static final String strNULL = "NULL";

	/**
	 * Constructs CSVReader using a comma for the separator.
	 * 
	 * @param reader the reader to an underlying CSV source.
	 */
	public CSVReader(Reader reader) {
		this(reader, DEFAULT_SEPARATOR);
	}

	/**
	 * Constructs CSVReader with supplied separator.
	 * 
	 * @param reader the reader to an underlying CSV source.
	 * @param separator the delimiter to use for separating entries.
	 */
	public CSVReader(Reader reader, char separator) {
		this(reader, separator, DEFAULT_QUOTE_CHARACTER);
	}

	/**
	 * Constructs CSVReader with supplied separator and quote char.
	 * 
	 * @param reader the reader to an underlying CSV source.
	 * @param separator the delimiter to use for separating entries
	 * @param quotechar the character to use for quoted elements
	 */
	public CSVReader(Reader reader, char separator, char quotechar) {
		this(reader, separator, quotechar, DEFAULT_SKIP_LINES);
	}

	/**
	 * Constructs CSVReader with supplied separator and quote char.
	 * 
	 * @param reader the reader to an underlying CSV source.
	 * @param separator the delimiter to use for separating entries
	 * @param quotechar the character to use for quoted elements
	 * @param line the line number to skip for start reading
	 */
	public CSVReader(Reader reader, char separator, char quotechar, int line) {
		this.br = new BufferedReader(reader);
		this.separator = separator;
		this.quotechar = quotechar;
		this.skipLines = line;
	}

	/**
	 * Reads the entire file into a List with each element being a String[] of
	 * tokens.
	 * 
	 * @return a List of String[], with each String[] representing a line of the
	 *         file.
	 * 
	 * @throws IOException if bad things happen during the read
	 */
	public List<String[]> readAll() throws IOException {

		List<String[]> allElements = new ArrayList<String[]>();
		while (hasNext) {
			String[] nextLineAsTokens = readNext();
			if (nextLineAsTokens != null)
				allElements.add(nextLineAsTokens);
		}
		return allElements;

	}

	/**
	 * Reads the next line from the buffer and converts to a string array.
	 * 
	 * @return a string array with each comma-separated element as a separate
	 *         entry.
	 * 
	 * @throws IOException if bad things happen during the read
	 */
	public String[] readNext() throws IOException {

		String nextLine = getNextLine();
		return hasNext ? parseLine(nextLine) : null;
	}

	/**
	 * Reads the next line from the file.
	 * 
	 * @return the next line from the file without trailing newline
	 * @throws IOException if bad things happen during the read
	 */
	private String getNextLine() throws IOException {
		if (!this.linesSkiped) {
			for (int i = 0; i < skipLines; i++) {
				br.readLine();
			}
			this.linesSkiped = true;
		}
		String nextLine = br.readLine();
		if (nextLine == null) {
			hasNext = false;
		}
		return hasNext ? nextLine : null;
	}

	/**
	 * Parses an incoming String and returns an array of elements.
	 * 
	 * @param nextLine the string to parse
	 * @return the comma-tokenized list of elements, or null if nextLine is null
	 * @throws IOException if bad things happen during the read
	 */
	private String[] parseLine(String nextLine) throws IOException {

		if (nextLine == null) {
			return null;
		}

		List<String> tokensOnThisLine = new ArrayList<String>();
		StringBuffer sb = new StringBuffer();
		boolean inQuotes = false;
		boolean startWithQuote = false;
		boolean endWithQuote = false;

		do {
			if (inQuotes) {
				// continuing a quoted section, reappend newline
				sb.append("\n");
				nextLine = getNextLine();
				if (nextLine == null)
					break;
			}

			for (int i = 0; i < nextLine.length(); i++) {

				char c = nextLine.charAt(i);
				if (c == quotechar) {
					if (sb.length() == 0) {
						startWithQuote = true;
					} else {
						if (nextLine.length() == i + 1) {
							endWithQuote = true;
						}
					}
					if (inQuotes && nextLine.length() > (i + 1)
							&& nextLine.charAt(i + 1) == quotechar) {
						sb.append(nextLine.charAt(i + 1));
						i++;
					} else {
						inQuotes = !inQuotes;
						if (i > 2 && nextLine.charAt(i - 1) != this.separator
								&& nextLine.length() > (i + 1)
								&& nextLine.charAt(i + 1) != this.separator) {
							sb.append(c);
						}
					}
				} else if (c == separator && !inQuotes) {
					if (nextLine.charAt(i - 1) == quotechar) {
						endWithQuote = true;
					}
					String str = sb.toString();
					if (startWithQuote && endWithQuote) {
						tokensOnThisLine.add(str);
					} else {
						if (str.equals(strNULL)) {
							str = null;
						}
						tokensOnThisLine.add(str);
					}
					sb = new StringBuffer(); // start work on next token
					startWithQuote = false;
					endWithQuote = false;
				} else {
					sb.append(c);
				}
			}
		} while (inQuotes);
		String str = sb.toString();
		if (startWithQuote && endWithQuote) {
			tokensOnThisLine.add(str);
		} else {
			if (str.equals(strNULL)) {
				str = null;
			}
			tokensOnThisLine.add(str);
		}
		return (String[]) tokensOnThisLine.toArray(new String[0]);

	}

	/**
	 * Closes the underlying reader.
	 * 
	 * @throws IOException if the close fails
	 */
	public void close() throws IOException {
		br.close();
	}

}
