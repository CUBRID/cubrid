#include <stdio.h>
#include "dlg.h"
#ifdef MEMCHK
#include "trax.h"
#else
#ifdef __STDC__
#include <stdlib.h>
#else
#include <malloc.h>
#endif /* __STDC__ */
#endif

int	err_found = 0;			/* indicates whether problem found */

void internal_error(s,file,line)
char *s,*file;
int line;
{
	fprintf(stderr,s,file,line);
	exit(1);
}

char *dlg_malloc(bytes,file,line)
int bytes;
char *file;
int line;
{
	char *t;

	t = (char *) malloc(bytes);
	if (!t){
		/* error */
		internal_error("%s(%d): unable to allocate memory\n",
			file,line);
	}
	return t;
}


char *dlg_calloc(n,bytes,file,line)
int n,bytes;
char *file;
int line;
{
	char *t;

	t = (char *) calloc(n,bytes);
	if (!t){
		/* error */
		internal_error("%s(%d): unable to allocate memory\n",
			file,line);
	}
	return t;
}


FILE *read_stream(name)
char *name;
{
	FILE *f;

	if (name){
		if (name[0] == '-') {
			fprintf(stderr, "dlg: invalid option: '%s'\n", name);
			f = NULL;
		}else{
			f = fopen(name, "r");
			if (f == NULL){
				/* couldn't open file */
				fprintf(stderr,
					"dlg: Warning: Can't read file %s.\n",
					name);
			}
		}
	}else{
		/* open stdin if nothing there */
		f = stdin;
	}
	return f;
}

FILE *write_stream(name)
char *name;
{
	FILE *f;

	if (name){
		if (name[0] == '-') {
			fprintf(stderr, "dlg: invalid option: '%s'\n", name);
			f = NULL;
		}else{
			f = fopen(name, "w");
			if (f == NULL){
				/* couldn't open file */
				fprintf(stderr,
					"dlg: Warning: Can't write to file %s.\n",
					name);
			}
		}
	}else{
		/* open stdin if nothing there */
		f = stdout;
	}
	return f;
}


void fatal(message,line_no)
char *message;
int line_no;
{
	fprintf(stderr,"Fatal : %s, line : %d\n",message,line_no);
	exit(2);
}

void error(message,line_no)
char *message;
int line_no;
{
	fprintf(stderr,"\"%s\", line %d: %s\n",
		(file_str[0] ? file_str[0] : "stdin"), line_no, message);
	err_found = 1;
}

void warning(message,line_no)
char *message;
int line_no;
{
	fprintf(stderr,"Warning : %s, line : %d\n",message,line_no);
}
