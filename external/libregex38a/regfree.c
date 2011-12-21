#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "include/regex38a.h"
#include "regmem.ih"
#include "regex2.h"

/*
 - cub_regfree - free everything
 = extern void cub_regfree(cub_regex_t *);
 */
void
cub_regfree(preg)
cub_regex_t *preg;
{
	register struct cub_re_guts *g;
	if (!cub_malloc_ok ())
		return;

	if (preg->re_magic != MAGIC1)	/* oops */
		return;			/* nice to complain, but hard */

	g = preg->re_g;
	if (g == NULL || g->magic != MAGIC2)	/* oops again */
		return;
	preg->re_magic = 0;		/* mark it invalid */
	g->magic = 0;			/* mark it invalid */

	if (g->strip != NULL)
		cub_free(NULL, (char *)g->strip);
	if (g->sets != NULL)
		cub_free(NULL, (char *)g->sets);
	if (g->setbits != NULL)
		cub_free(NULL, (char *)g->setbits);
	if (g->must != NULL)
		cub_free(NULL, g->must);
	cub_free(NULL, (char *)g);
}
