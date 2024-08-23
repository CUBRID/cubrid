/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * stack_dump.c - call stack dump
 */

#ident "$Id$"

#include "printer.hpp"

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
#elif defined(LINUX)
#if __WORDSIZE == 32
#include <stdio.h>
#include <string.h>

#include <ucontext.h>
#include <dlfcn.h>

#include "error_code.h"
#include "memory_hash.h"
#include "stack_dump.h"
#else // __WORDSIZE == 32
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <execinfo.h>

#include "error_code.h"
#include "memory_hash.h"
#include "stack_dump.h"
#endif // __WORDSIZE == 32
#else // LINUX
#include <stdio.h>
#include "stack_dump.h"
#endif // x86_SOLARIS, LINUX

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(x86_SOLARIS)
#define FRAME_PTR_REGISTER      EBP
#define TR_ARG_MAX              6
#define PGRAB_RDONLY            0x04
#define MIN(a,b)                ((a) < (b) ? (a) : (b))

typedef Elf64_Sym GElf_Sym;

extern struct ps_prochandle *Pgrab (pid_t, int, int *);
extern void Pfree (struct ps_prochandle *);
extern int Plookup_by_addr (struct ps_prochandle *, uintptr_t, char *, size_t, GElf_Sym *);


static ulong_t argcount (uintptr_t eip);
static int read_safe (int fd, struct frame *fp, struct frame **savefp, uintptr_t * savepc);


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
read_safe (int fd, struct frame *fp, struct frame **savefp, uintptr_t * savepc)
{
  uintptr_t newfp;

  if ((uintptr_t) fp & (sizeof (void *) - 1))
    {
      return (-1);		/* misaligned */
    }

  if ((pread (fd, (void *) &newfp, sizeof (fp->fr_savfp), (off_t) & fp->fr_savfp) != sizeof (fp->fr_savfp))
      || pread (fd, (void *) savepc, sizeof (fp->fr_savpc), (off_t) & fp->fr_savpc) != sizeof (fp->fr_savpc))
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
 *   output(in/out):
 *   pc(in):
 *   argc(in):
 *   argv(in):
 *   Pr(in):
 */
static int
log_stack_info (print_output & output, uintptr_t pc, ulong_t argc, long *argv, struct ps_prochandle *Pr)
{
  char buff[255];
  GElf_Sym sym;
  uintptr_t start;
  int i;

  sprintf (buff, "%.*lx", 8, (long) pc);
  strcpy (buff + 8, " ????????");

  if (Plookup_by_addr (Pr, pc, buff + 1 + 8, sizeof (buff) - 1 - 8, &sym) == 0)
    {
      start = sym.st_value;
    }
  else
    {
      start = pc;
    }

  output ("%-17s(", buff);

  for (i = 0; i < argc; i++)
    {
      output ((i + 1 == argc) ? "%lx" : "%lx, ", argv[i]);
    }

  output ((start != pc) ? ") + %lx\n" : ")\n", (long) (pc - start));
  output.flush ();

  return (0);
}

/*
 * er_dump_call_stack_internal - dump call stack
 *   return: none
 *   output(in/put):
 */
void
er_dump_call_stack_internal (print_output & output)
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

  fp = (struct frame *) ((caddr_t) ucp.uc_mcontext.gregs[FRAME_PTR_REGISTER] + STACK_BIAS);

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

      log_stack_info (output, savepc, argc, argv, Pr);
      fp = savefp;
    }

  close (fd);
  Pfree (Pr);
}

#elif defined(LINUX)
static int er_resolve_function_name (const void *address, const char *lib_file_name, char *buffer, int buffer_size);

#if __WORDSIZE == 32

#define PEEK_DATA(addr)	(*(size_t *)(addr))
#define MAXARGS		6
#define BUFFER_SIZE     1024

/*
 * er_dump_call_stack_internal - dump call stack
 *   return:
 *   output(in/out):
 */
void
er_dump_call_stack_internal (print_output & output)
{
  ucontext_t ucp;
  size_t frame_pointer_addr, next_frame_pointer_addr;
  size_t return_addr, arg;
  int i, nargs;
  Dl_info dl_info;
  const char *func_name_p;
  const void *func_addr_p = NULL;
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
	  func_addr_p = (void *) ((size_t) ((const char *) return_addr) - (size_t) dl_info.dli_fbase);
	}
      else
	{
	  func_addr_p = (void *) return_addr;
	}

      if (dl_info.dli_sname)
	{
	  func_name_p = dl_info.dli_sname;
	}
      else
	{
	  if (er_resolve_function_name (func_addr_p, dl_info.dli_fname, buffer, sizeof (buffer)) == NO_ERROR)
	    {
	      func_name_p = buffer;
	    }
	  else
	    {
	      func_name_p = "???";
	    }
	}

      output ("%s(%p): %s", dl_info.dli_fname, func_addr_p, func_name_p);

      next_frame_pointer_addr = PEEK_DATA (frame_pointer_addr);
      nargs = (next_frame_pointer_addr - frame_pointer_addr - 8) / 4;
      if (nargs > MAXARGS)
	{
	  nargs = MAXARGS;
	}

      output (" (");
      if (nargs > 0)
	{
	  for (i = 1; i <= nargs; i++)
	    {
	      arg = PEEK_DATA (frame_pointer_addr + 4 * (i + 1));
	      output ("%x", arg);
	      if (i < nargs)
		{
		  output (", ");
		}
	    }
	}
      output (")\n");

      if (next_frame_pointer_addr == 0)
	{
	  break;
	}

      return_addr = PEEK_DATA (frame_pointer_addr + 4);
      frame_pointer_addr = next_frame_pointer_addr;
    }

  output.flush ();
}

#else /* __WORDSIZE == 32 */

#define MAX_TRACE       32
#define BUFFER_SIZE     1024

/*
 * er_dump_call_stack_internal - dump call stack
 *   return:
 *   output(in/out):
 */
void
er_dump_call_stack_internal (print_output & output)
{
  void *return_addr[MAX_TRACE];
  int i, trace_count;
  Dl_info dl_info;
  const char *func_name_p;
  const void *func_addr_p;
  char buffer[BUFFER_SIZE];

  trace_count = backtrace (return_addr, MAX_TRACE);

  for (i = 0; i < trace_count; i++)
    {
      if (dladdr (return_addr[i], &dl_info) == 0)
	{
	  break;
	}

      if (dl_info.dli_fbase >= (const void *) 0x40000000)
	{
	  func_addr_p = (void *) ((size_t) ((const char *) return_addr[i]) - (size_t) dl_info.dli_fbase);
	}
      else
	{
	  func_addr_p = return_addr[i];
	}

      if (dl_info.dli_sname)
	{
	  func_name_p = dl_info.dli_sname;
	}
      else
	{
	  if (er_resolve_function_name (func_addr_p, dl_info.dli_fname, buffer, sizeof (buffer)) == NO_ERROR)
	    {
	      func_name_p = buffer;
	    }
	  else
	    {
	      func_name_p = "???";
	    }
	}

      output ("%s(%p): %s\n", dl_info.dli_fname, func_addr_p, func_name_p);
    }

  output.flush ();
}
#endif /* __WORDSIZE == 32 */

MHT_TABLE *fname_table;

static int
er_resolve_function_name (const void *address, const char *lib_file_name_p, char *buffer, int buffer_size)
{
  FILE *output;
  char cmd_line[BUFFER_SIZE];
  char *func_name_p, *pos;
  char buf[BUFFER_SIZE], *key, *data;

  snprintf (buf, BUFFER_SIZE, "%p%s", address, lib_file_name_p);
  data = (char *) mht_get (fname_table, buf);
  if (data != NULL)
    {
      snprintf (buffer, buffer_size, data);
      return NO_ERROR;
    }

  snprintf (cmd_line, sizeof (cmd_line), "addr2line -f -C -e %s %p 2>/dev/null", lib_file_name_p, address);

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

  key = strdup (buf);
  if (key == NULL)
    {
      return ER_FAILED;
    }

  data = strdup (func_name_p);
  if (data == NULL)
    {
      free (key);
      return ER_FAILED;
    }

  if (mht_put (fname_table, key, data) == NULL)
    {
      free (key);
      free (data);
      return ER_FAILED;
    }
  return NO_ERROR;
}
#else /* LINUX */
/*
 * er_dump_call_stack_internal - dump call stack
 *   return:
 *   output(in/out):
 */
void
er_dump_call_stack_internal (print_output & output)
{
  output ("call stack dump: NOT available in this platform\n");
}
#endif /* X86_SOLARIS, LINUX */

void
er_dump_call_stack (FILE * outfp)
{
  file_print_output output (outfp);
  er_dump_call_stack_internal (output);
}

char *
er_dump_call_stack_to_string (void)
{
  string_print_output output;
  er_dump_call_stack_internal (output);
  char *ptr = strdup (output.get_buffer ());
  output.clear ();

  return ptr;
}
