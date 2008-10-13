
lib_LTLIBRARIES = libxmlrpc-epi.la

libxmlrpc_epi_la_LIBADD=$(EXPAT_LIB_LOCATION)/libexpat.la

libxmlrpc_epi_la_SOURCES = \
	base64.c \
	encodings.c \
	queue.c \
	simplestring.c \
	xml_element.c \
	xml_to_dandarpc.c \
	xml_to_xmlrpc.c \
        xml_to_soap.c \
	xmlrpc.c \
	xmlrpc_introspection.c \
        system_methods.c
        
libxmlrpc_epi_la_LDFLAGS = -version-info 0:3:0
libxmlrpc_epi_incdir=$(includedir)/xmlrpc-epi 
include_HEADERS = \
	base64.h \
	encodings.h \
	queue.h \
	simplestring.h \
	xml_element.h \
	xml_to_xmlrpc.h \
	xmlrpc.h \
	xmlrpc_introspection.h
	
SHELL = /bin/sh

ROBODOC=robodoc
ROBOOPTS=C SORT 

# Your source files.
#
DOCSOURCES= \
	$(include_HEADERS) \
	$(libxmlrpc_epi_la_SOURCES)

# The name of your Project
#
PROJECT=xmlrpc-epi

# The various documentation files, derived from the source files.
# HTML
#
HTMLDOCS=$(DOCSOURCES:=.html)
HTMLXREFS=$(HTMLDOCS:.html=.html.xref)
HTMLXREFSFILE=$(PROJECT)_html.xrefs
# LATEX
#
LATEXDOCS=$(HTMLDOCS:=.tex)
LATEXXREFS=$(LATEXDOCS:.tex=.tex.xref)
LATEXXREFSFILE=$(PROJECT)_tex.xrefs
# ASCII
#
ASCIIDOCS=$(DOCSOURCES:=.txt)
# RTF
#
RTFDOCS=$(DOCSOURCES:=.rtf)
RTFXREFS=$(RTFDOCS:.rtf=.rtf.xref)
RTFXREFSFILE=$(PROJECT)_rtf.xrefs

# Some common targets
xrefall: xhtml xtex xrtf
docall: html tex ascii rtf

# Create the xref files for the various formats.
xhtml: $(HTMLXREFSFILE) 
xtex: $(LATEXXREFSFILE) 
xrtf: $(RTFXREFSFILE)

# Create the documentation files for the various formats.
html: xhtml $(HTMLDOCS) $(PROJECT)_mi.html
tex: xtex $(LATEXDOCS) $(PROJECT)_mi.tex
rtf: xrtf $(RTFDOCS)
ascii: xascii $(ASCIIDOCS)

# master index file, currently works only for html and latex documentation.
# Note that you can define the title of the document.
$(PROJECT)_mi.html: $(HTMLXREFSFILE) 
	$(ROBODOC) $< $@ INDEX HTML TOC SORT FOLD C TITLE "$(PROJECT) Master Index"

$(PROJECT)_mi.tex: $(LATEXXREFSFILE)
	$(ROBODOC) $< $@ INDEX LATEX TITLE "$(PROJECT) API Reference"

# create xrefs file (file with the names of all .xref files).
$(HTMLXREFSFILE) : $(HTMLXREFS)
	/bin/ls $(HTMLXREFS) > $@
$(LATEXXREFSFILE) : $(LATEXXREFS)
	/bin/ls  $(LATEXXREFS) > $@
$(RTFXREFSFILE) : $(RTFXREFS)
	/bin/ls  $(RTFXREFS) > $@

# Rule to create an .xref file from a source file for the various formats.
*.html.xref : *
	$(ROBODOC) $< $(@:.xref=) $(ROBOOPTS) INTERNAL GENXREF $@
*.tex.xref : *
	$(ROBODOC) $< $(@:.xref=) $(ROBOOPTS) INTERNAL GENXREF $@
*.rtf.xref : *
	$(ROBODOC) $< $(@:.xref=) $(ROBOOPTS) INTERNAL GENXREF $@

# Rule to create html documentation from a source file.
*.html : *
	$(ROBODOC) $< $@ HTML $(ROBOOPTS) XREF $(HTMLXREFSFILE)

# Rule to create latex documentation from a source file.
# We do not include source items, and generate laxtex documents
# than can be included in a master document.
*.tex : *
	$(ROBODOC) $< $@ LATEX $(ROBOOPTS) NOSOURCE SINGLEDOC XREF $(LATEXXREFSFILE)

# Rule to create ascii documentation from a source file.
*.txt : *
	$(ROBODOC) $< $@ ASCII 

# Rule to create rtf documentation from a source file.
*.rtf : *
	$(ROBODOC) $< $@ RTF $(ROBOOPTS) XREF $(RTFXREFSFILE)

# Use netscape to view the master index file for our project.
htmlview: html
	netscape $(PROJECT)_mi.html

# Use the latex programs to generate a .dvi from the master index file
# for our prokect. View this .dvi file with xdvi
texview:  tex
	latex $(PROJECT)_mi
	makeindex $(PROJECT)_mi
	latex $(PROJECT)_mi
	latex $(PROJECT)_mi
	xdvi  $(PROJECT)_mi.dvi

# Clean-up the mess we made
#
doc_clean:
	rm -f $(HTMLXREFS) 
	rm -f $(HTMLDOCS) 
	rm -f $(LATEXXREFS)
	rm -f $(LATEXDOCS) 
	rm -f $(PROJECT)_mi.* *.aux
	rm -f $(RTFXREFS)
	rm -f $(RTFDOCS)
	rm -f $(ASCIIDOCS)
	rm -f $(HTMLXREFSFILE) 
	rm -f $(LATEXXREFSFILE) 
	rm -f $(RTFXREFSFILE)
