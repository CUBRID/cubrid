/* Main function for dlg version
 *
 * Will Cohen
 * 8/23/90
 */

#include <stdio.h>
#include "stdpccts.h"
#include "dlg_proto.h"

char	program[] = "dlg";
char	version[] = "1.10";
int	numfiles = 0;
char	*file_str[2] = {NULL, NULL};
char	*mode_file = "mode.h";

/* Option variables */
int comp_level = 0;
int interactive = FALSE;
int case_insensitive = FALSE;
int warn_ambig = FALSE;

/* Option List Stuff */
void p_comp0()		{comp_level = 0;}
void p_comp1()		{comp_level = 1;}
void p_comp2()		{comp_level = 2;}
void p_stdio()		{ file_str[numfiles++] = NULL;}
void p_file(s) char *s;	{ file_str[numfiles++] = s;}
void p_mode_file(s,t) char *s,*t;{mode_file=t;}
void p_ansi()		{gen_ansi = TRUE;}
void p_interactive()	{interactive = TRUE;}
void p_case_s()		{ case_insensitive = FALSE; }
void p_case_i()		{ case_insensitive = TRUE; }
void p_warn_ambig()	{ warn_ambig = TRUE; }

typedef struct {
			char *option;
			int  arg;
			void (*process)();
			char *descr;
		} Opt;

Opt options[] = {
	{ "-C0", 0, p_comp0, "No compression (default)" },
	{ "-C1", 0, p_comp1, "Compression level 1" },
	{ "-C2", 0, p_comp2, "Compression level 2" },
	{ "-ga", 0, p_ansi, "Generate ansi C"},
	{ "-Wambiguity", 0, p_warn_ambig, "Warn if expressions ambiguous"},
	{ "-m", 1, p_mode_file, "Rename lexical mode output file"},
	{ "-i", 0, p_interactive, "Build interactive scanner"},
	{ "-ci", 0, p_case_i, "Make lexical analyzer case insensitive"},
	{ "-cs", 0, p_case_s, "Make lexical analyzer case sensitive (default)"},
	{ "-", 0, p_stdio, "Use standard i/o rather than file"},
	{ "*", 0, p_file, ""}, /* anything else is a file */
	{ NULL, 0, NULL }	
};

void init();
void ProcessArgs(int argc, char **argv, Opt *options);

int main(argc,argv)
int argc;
char *argv[];
{
	init();
	fprintf(stderr, "%s  Version %s   1989-1993\n", &(program[0]),
		&(version[0]));
	if ( argc == 1 ) 
	{
		Opt *p = options;
		fprintf(stderr, "%s [options] f1 f2 ... fn\n",argv[0]);
		while ( *(p->option) != '*' )
		{
			fprintf(stderr, "\t%s %s\t%s\n",
							p->option,
							(p->arg)?"___":"   ",
							p->descr);
			p++;
		}
	}else{
		ProcessArgs(argc-1, &(argv[1]), options);
		if (input_stream = read_stream(file_str[0])){
			/* don't overwrite unless input okay */
			output_stream = write_stream(file_str[1]);
			mode_stream = write_stream(mode_file);
		}
		/* make sure that error reporting routines in grammar
		   know what the file really is */
		/* make sure that reading and writing somewhere */
		if (input_stream && output_stream && mode_stream){
			ANTLR(grammar(), input_stream);
		}
	}
	exit(err_found);
}

void ProcessArgs(argc, argv, options)
int argc;
char **argv;
Opt *options;
{
	Opt *p;
	
	while ( argc-- > 0 )
	{
		p = options;
		while ( p->option != NULL )
		{
			if ( strcmp(p->option, "*") == 0 ||
				 strcmp(p->option, *argv) == 0 )
			{
				if ( p->arg )
				{
					(*p->process)( *argv, *(argv+1) );
					argv++;
					argc--;
				}
				else
					(*p->process)( *argv );
				break;
			}
			p++;
		}
		argv++;
	}
}

/* initialize all the variables */
void init()
{
	register int i;

	used_chars = empty;
	used_classes = empty;
	/* make the valid character set */
	normal_chars = empty;
	/* NOTE: MIN_CHAR is EOF */
	/* NOTE: EOF is not quite a valid char, it is special. Skip it*/
	for (i = 1; i<CHAR_RANGE; ++i){
		set_orel(i,&normal_chars);
	}
	make_nfa_model_node();
	clear_hash();
	/* NOTE: need to set this flag before the lexer starts getting */
	/* tokens */
   	func_action = FALSE;	
}

/* stuff that needs to be reset when a new automaton is being built */
void new_automaton_mode()
{
	set_free(used_chars);
	clear_hash();
}
