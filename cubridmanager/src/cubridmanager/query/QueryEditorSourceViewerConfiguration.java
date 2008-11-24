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

package cubridmanager.query;

import org.eclipse.jface.text.IDocument;
import org.eclipse.jface.text.presentation.IPresentationReconciler;
import org.eclipse.jface.text.presentation.PresentationReconciler;
import org.eclipse.jface.text.rules.DefaultDamagerRepairer;
import org.eclipse.jface.text.source.ISourceViewer;
import org.eclipse.jface.text.source.SourceViewerConfiguration;

import cubridmanager.query.view.QueryEditor;

/**
 * This class provides the source viewer configuration
 */
public class QueryEditorSourceViewerConfiguration extends
		SourceViewerConfiguration {
	/**
	 * Gets the presentation reconciler. This will color the code.
	 */
	public IPresentationReconciler getPresentationReconciler(
			ISourceViewer sourceViewer) {
		// Create the presentation reconciler
		PresentationReconciler reconciler = new PresentationReconciler();
		reconciler
				.setDocumentPartitioning(getConfiguredDocumentPartitioning(sourceViewer));

		// Create the damager/repairer for comment partitions
		DefaultDamagerRepairer dr = new DefaultDamagerRepairer(
				new CommentScanner());
		reconciler.setDamager(dr, QueryPartitionScanner.COMMENT);
		reconciler.setRepairer(dr, QueryPartitionScanner.COMMENT);

		// Create the damager/repairer for default
		dr = new DefaultDamagerRepairer(QueryEditor.getApp().getCodeScanner());
		reconciler.setDamager(dr, IDocument.DEFAULT_CONTENT_TYPE);
		reconciler.setRepairer(dr, IDocument.DEFAULT_CONTENT_TYPE);

		return reconciler;
	}

	/**
	 * Gets the configured document partitioning
	 * 
	 * @return String
	 */
	public String getConfiguredDocumentPartitioning(ISourceViewer sourceViewer) {
		return QueryEditor.QUERY_PARTITIONING;
	}

	/**
	 * Gets the configured partition types
	 * 
	 * @return String[]
	 */
	public String[] getConfiguredContentTypes(ISourceViewer sourceViewer) {
		return new String[] { IDocument.DEFAULT_CONTENT_TYPE,
				QueryPartitionScanner.COMMENT };
	}
}
