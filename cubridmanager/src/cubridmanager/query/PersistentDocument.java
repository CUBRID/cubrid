package cubridmanager.query;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;

import org.eclipse.jface.text.Document;
import org.eclipse.jface.text.DocumentEvent;
import org.eclipse.jface.text.IDocumentListener;

/**
 * This class adds persistence to the Document class
 */
public class PersistentDocument extends Document implements IDocumentListener {
	private String fileName;

	private boolean dirty;

	/**
	 * PersistentDocument constructor
	 */
	public PersistentDocument() {
		addDocumentListener(this);
	}

	/**
	 * Gets whether this document is dirty
	 * 
	 * @return boolean
	 */
	public boolean isDirty() {
		return dirty;
	}

	/**
	 * Gets the file name
	 * 
	 * @return String
	 */
	public String getFileName() {
		return fileName;
	}

	/**
	 * Sets the file name
	 * 
	 * @param fileName
	 */
	public void setFileName(String fileName) {
		this.fileName = fileName;
	}

	/**
	 * Saves the file
	 * 
	 * @throws IOException
	 *             if any problems
	 */
	public void save() throws IOException {
		if (fileName == null)
			throw new IllegalStateException("Can't save file with null name");
		BufferedWriter out = null;
		try {
			out = new BufferedWriter(new FileWriter(fileName));
			out.write(get());
			dirty = false;
		} finally {
			try {
				if (out != null)
					out.close();
			} catch (IOException e) {
			}
		}
	}

	/**
	 * Opens the file
	 * 
	 * @throws IOException
	 *             if any problems
	 */
	public void open() throws IOException {
		if (fileName == null)
			throw new IllegalStateException("Can't open file with null name");
		BufferedReader in = null;
		try {
			in = new BufferedReader(new FileReader(fileName));
			StringBuffer buf = new StringBuffer();
			int n;
			while ((n = in.read()) != -1) {
				buf.append((char) n);
			}
			set(buf.toString());
			dirty = false;
		} finally {
			try {
				if (in != null)
					in.close();
			} catch (IOException e) {
			}
		}
	}

	/**
	 * Clears the file's contents
	 */
	public void clear() {
		set("");
		fileName = "";
		dirty = false;
	}

	/**
	 * Called when the document is about to be changed
	 * 
	 * @param event
	 *            the event
	 */
	public void documentAboutToBeChanged(DocumentEvent event) {

	}

	/**
	 * Called when the document changes
	 * 
	 * @param event
	 *            the event
	 */
	public void documentChanged(DocumentEvent event) {
		// Document has changed; make it dirty
		dirty = true;
	}
}
