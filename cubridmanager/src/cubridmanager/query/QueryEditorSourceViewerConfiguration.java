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
