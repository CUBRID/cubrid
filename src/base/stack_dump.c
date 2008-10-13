/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * stackdump.c - call stack dump
 */

#ident "$Id$"

#if defined(x86_SOLARIS)

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <ucontext.h>
#include <sys/stack.h>
#include <sys/frame.h>
#include <sys/elf.h>

#include "stack_dump.h"

#define FRAME_PTR_REGISTER      EBP
#define TR_ARG_MAX              6
#define PGRAB_RDONLY            0x04
#define MIN(a,b)                ((a) < (b) ? (a) : (b))

typedef Elf64_Sym GElf_Sym;

extern struct ps_prochandle *Pgrab (pid_t, int, int *);
extern void Pfree (struct ps_prochandle *);
extern int Plookup_by_addr (struct ps_prochandle *, uintptr_t, char *, size_t,
			    GElf_Sym *);


static ulong_t argcount (uintptr_t eip);
static int read_safe (int fd, struct frame *fp, struct frame **savefp,
		      uintptr_t * savepc);


/*
 * argcount -
 *   return:
 *   eip(in):
 */
static ulong_t
argcount (uintptr_t eip)
{
  const uint8_t *ins = (const uint8_t *) eip;
  ulong_t n;

  enum
  {
    M_MODRM_ESP = 0xc4,		/* Mod/RM byte indicates %esp */
    M_ADD_IMM32 = 0x81,		/* ADD imm32 to r/m32 */
    M_ADD_IMM8 = 0x83		/* ADD imm8 to r/m32 */
  };

  switch (ins[0])
    {
    case M_ADD_IMM32:
      n = ins[2] + (ins[3] << 8) + (ins[4] << 16) + (ins[5] << 24);
      break;

    case M_ADD_IMM8:
      n = ins[2];
      break;

    default:
      return (TR_ARG_MAX);
    }

  n /= sizeof (long);
  return (MIN (n, TR_ARG_MAX));
}

/*
 * read_safe -
 *   return:
 *   fd(in):
 *   fp(in):
 *   savefp(in):
 *   savepc(in):
 */
static int
read_safe (int fd, struct frame *fp, struct frame **savefp,
	   uintptr_t * savepc)
{
  uintptr_t newfp;

  if ((uintptr_t) fp & (sizeof (void *) - 1))
    {
      return (-1);		/* misaligned */
    }

  if ((pread (fd, (void *) &newfp, sizeof (fp->fr_savfp),
	      (off_t) & fp->fr_savfp) != sizeof (fp->fr_savfp)) ||
      pread (fd, (void *) savepc, sizeof (fp->fr_savpc),
	     (off_t) & fp->fr_savpc) != sizeof (fp->fr_savpc))
    {
      return (-1);
    }

  if (newfp != 0)
    {
      newfp += STACK_BIAS;
    }

  *savefp = (struct frame *) newfp;

  return (0);
}

/*
 * log_stack_info -
 *   return:
 *   logfile(in):
 *   pc(in):
 *   argc(in):
 *   argv(in):
 *   Pr(in):
 */
static int
log_stack_info (FILE * logfile, uintptr_t pc, ulong_t argc, long *argv,
		struct ps_prochandle *Pr)
{
  char buff[255];
  GElf_Sym sym;
  uintptr_t start;
  int i;

  sprintf (buff, "%.*lx", 8, (long) pc);
  strcpy (buff + 8, " ????????");

  if (Plookup_by_addr (Pr, pc, buff + 1 + 8, sizeof (buff) - 1 - 8, &sym) ==
      0)
    {
      start = sym.st_value;
    }
  else
    {
      start = pc;
    }

  fprintf (logfile, "%-17s(", buff);

  for (i = 0; i < argc; i++)
    {
      fprintf (logfile, (i + 1 == argc) ? "%lx" : "%lx, ", argv[i]);
    }

  fprintf (logfile, (start != pc) ? ") + %lx\n" : ")\n", (long) (pc - start));
  fflush (logfile);

  return (0);
}

/*
 * er_dump_call_stack - dump call stack
 *   return: none
 *   logfile(in):
 */
void
er_dump_call_stack (FILE * outfp)
{
  ucontext_t ucp;
  struct frame *fp, *savefp;
  ulong_t argc;
  long *argv;
  struct ps_prochandle *Pr;
  int err, fd;
  uintptr_t savepc;

  Pr = Pgrab (getpid (), PGRAB_RDONLY, &err);
  if (Pr == NULL)
    {
      return;
    }

  if (getcontext (&ucp) < 0)
    {
      Pfree (Pr);
      return;
    }

  fp = (struct frame *) ((caddr_t) ucp.uc_mcontext.gregs[FRAME_PTR_REGISTER] +
			 STACK_BIAS);

  fd = open ("/proc/self/as", O_RDONLY);
  if (fd < 0)
    {
      Pfree (Pr);
      return;
    }

  while (fp != NULL)
    {
      if (read_safe (fd, fp, &savefp, &savepc) != 0)
	{
	  close (fd);
	  Pfree (Pr);
	  return;
	}

      /* break when reaches the bottom of stack */
      if (savefp == NULL)
	{
	  break;
	}

      if (savefp->fr_savfp == 0)
	{
	  break;
	}

      argc = argcount (savepc);
      argv = (long *) ((char *) savefp + sizeof (struct frame));

      log_stack_info (outfp, savepc, argc, argv, Pr);
      fp = savefp;
    }

  close (fd);
  Pfree (Pr);
}

#elif defined(LINUX)

#include <stdio.h>
#include <string.h>

#define __USE_GNU
#include <ucontext.h>
#include <dlfcn.h>

#include "error_code.h"
#include "stack_dump.h"

#define PEEK_DATA(addr)	(*(size_t *)(addr))
#define MAXARGS		6
#define BUFFER_SIZE     1024

static int er_resolve_function_name (const void *address,
				     const char *lib_file_name, char *buffer,
				     int buffer_size);

static int
er_resolve_function_name (const void *address, const char *lib_file_name_p,
			  char *buffer, int buffer_size)
{
  FILE *output;
  char cmd_line[BUFFER_SIZE];
  char *func_name_p, *pos;

  snprintf (cmd_line, sizeof (cmd_line),
	    "addr2line -f -C -e $(which %s) %p 2>/dev/null",
	    lib_file_name_p, address);

  output = popen (cmd_line, "r");
  if (!output)
    {
      return ER_FAILED;
    }

  func_name_p = fgets (buffer, buffer_size - 1, output);
  if (!func_name_p || !func_name_p[0])
    {
      pclose (output);
      return ER_FAILED;
    }

  pos = strchr (func_name_p, '\n');
  if (pos)
    {
      pos[0] = '\0';
    }

  pclose (output);
  return NO_ERROR;
}

/*
 * er_dump_call_stack - dump call stack
 *   return:
 *   outfp(in):
 */
void
er_dump_call_stack (FILE * outfp)
{
  ucontext_t ucp;
  size_t frame_pointer_addr, next_frame_pointer_addr;
  size_t return_addr, arg;
  int i, nargs;
  Dl_info dl_info;
  const char *func_name_p;
  const void *func_addr_p;
  char buffer[BUFFER_SIZE];

  if (getcontext (&ucp) < 0)
    {
      return;
    }

  return_addr = ucp.uc_mcontext.gregs[REG_EIP];
  frame_pointer_addr = ucp.uc_mcontext.gregs[REG_EBP];

  while (frame_pointer_addr)
    {
      if (dladdr ((size_t *) return_addr, &dl_info) == 0)
	{
	  break;
	}

      if (dl_info.dli_fbase >= (const void *) 0x40000000)
	{
	  func_addr_p = (void *) ((size_t) ((const char *) return_addr) -
	                          (size_t) dl_info.dli_fbase);
	}

      if (dl_info.dli_sname)
	{
	  func_name_p = dl_info.dli_sname;
	}
      else
	{
	  if (er_resolve_function_name (func_addr_p, dl_info.dli_fname,
					buffer, sizeof (buffer)) == NO_ERROR)
	    {
	      func_name_p = buffer;
	    }
	  else
	    {
	      func_name_p = "???";
	    }
	}

      fprintf (outfp, "%s(%p): %s", dl_info.dli_fname, func_addr_p,
	       func_name_p);

      next_frame_pointer_addr = PEEK_DATA (frame_pointer_addr);
      nargs = (next_frame_pointer_addr - frame_pointer_addr - 8) / 4;
      if (nargs > MAXARGS)
	{
	  nargs = MAXARGS;
	}

      fprintf (outfp, " (");
      if (nargs > 0)
	{
	  for (i = 1; i <= nargs; i++)
	    {
	      arg = PEEK_DATA (frame_pointer_addr + 4 * (i + 1));
	      fprintf (outfp, "%x", arg);
	      if (i < nargs)
		{
		  fprintf (outfp, ", ");
		}
	    }
	}
      fprintf (outfp, ")\n");

      if (next_frame_pointer_addr == 0)
	{
	  break;
	}

      return_addr = PEEK_DATA (frame_pointer_addr + 4);
      frame_pointer_addr = next_frame_pointer_addr;
    }

  fflush (outfp);
}

#else /* LINUX */

#include <stdio.h>
#include "stack_dump.h"

/*
 * er_dump_call_stack - dump call stack
 *   return:
 *   outfp(in):
 */
void
er_dump_call_stack (FILE * outfp)
{
  fprintf (outfp, "call stack dump: NOT available in this platform\n");
}
#endif /* X86_SOLARIS, LINUX */
