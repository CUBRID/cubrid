/**
 * @file userealtls.cpp
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 *
 * This file leverages compiler support for thread-local variables for
 * access to thread-local heaps. It also intercepts thread completions
 * to flush these local heaps, returning any unused memory to the
 * global Hoard heap. On Windows, this happens in DllMain. On Unix
 * platforms, we interpose our own versions of pthread_create and
 * pthread_exit.
*/

#include "VERSION.h"

#if defined(_WIN32)
DWORD LocalTLABIndex;
#endif

#include <new>

// For now, we only use thread-local variables (__thread)
//   (a) for Linux x86-32 platforms with gcc version > 3.3.0, and
//   (b) when compiling with the SunPro compilers.

#if ((GCC_VERSION >= 30300) && !defined(__x86_64) && !defined(__SVR4) && !defined(__MACH__)) \
    || defined(__SUNPRO_CC)
#define USE_THREAD_KEYWORD 1
#endif


#if defined(USE_THREAD_KEYWORD)

static __thread double tlabBuffer[sizeof(TheCustomHeapType) / sizeof(double) + 1];

static __thread TheCustomHeapType * theTLAB = NULL;

static TheCustomHeapType * initializeCustomHeap (void) {
  new ((char *) &tlabBuffer) TheCustomHeapType (getMainHoardHeap());
  return (theTLAB = (TheCustomHeapType *) &tlabBuffer);
}

inline TheCustomHeapType * getCustomHeap (void) {
  // The pointer to the TLAB itself.
  theTLAB = (theTLAB ? theTLAB : initializeCustomHeap());
  return theTLAB;
}

#else

#if !defined(_WIN32)
static pthread_key_t theHeapKey;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

static void deleteThatHeap (void * p) {
  // Called when the thread goes away - reclaims the heap.
  TheCustomHeapType * heap = (TheCustomHeapType *) p;
  heap->~TheCustomHeapType();
  getMainHoardHeap()->free ((void *) heap);
}

static void make_heap_key (void)
{
  if (pthread_key_create (&theHeapKey, deleteThatHeap) != 0) {
    // This should never happen.
  }
}

static void createKey (void) {
  // Ensure that the key is initialized.
  pthread_once (&key_once, make_heap_key);
}

#endif


static TheCustomHeapType * initializeCustomHeap (void)
{
#if !defined(_WIN32)
  createKey();
  assert (pthread_getspecific (theHeapKey) == NULL);
#endif
  // Allocate a per-thread heap.
  TheCustomHeapType * heap;
  size_t sz = sizeof(TheCustomHeapType) + sizeof(double);
  void * mh = getMainHoardHeap()->malloc(sz);
  heap = new ((char *) mh) TheCustomHeapType (getMainHoardHeap());

  // Store it in the appropriate thread-local area.
#if defined(_WIN32)
  TlsSetValue (LocalTLABIndex, heap);
#else
  int r = pthread_setspecific (theHeapKey, (void *) heap);
  assert (!r);
#endif

  return heap;
}


inline TheCustomHeapType * getCustomHeap (void) {
  TheCustomHeapType * heap;
#if defined(_WIN32)
  heap = (TheCustomHeapType *) TlsGetValue (LocalTLABIndex);
#else
  createKey();
  heap = (TheCustomHeapType *) pthread_getspecific (theHeapKey);
#endif
  if (heap == NULL)  {
    heap = initializeCustomHeap();
  }
  return heap;
}



#endif

//
// Intercept thread creation and destruction to flush the TLABs.
//

#if defined(_WIN32)

#ifndef HOARD_PRE_ACTION
#define HOARD_PRE_ACTION
#endif

#ifndef HOARD_POST_ACTION
#define HOARD_POST_ACTION
#endif

#include <stdio.h>

#ifndef CUSTOM_DLLNAME
#define CUSTOM_DLLNAME DllMain
#endif

extern "C" {

BOOL WINAPI CUSTOM_DLLNAME (HANDLE hinstDLL, DWORD fdwReason, LPVOID lpreserved)
{
  int i;
  int tid;
  static int np = HL::CPUInfo::computeNumProcessors();

  switch (fdwReason) {

  case DLL_PROCESS_ATTACH:
    {
      LocalTLABIndex = TlsAlloc();
      if (LocalTLABIndex == TLS_OUT_OF_INDEXES) {
	// Not sure what to do here!
      }
      HOARD_PRE_ACTION;
      getCustomHeap();
    }
    break;

  case DLL_THREAD_ATTACH:
    if (np == 1) {
      // We have exactly one processor - just assign the thread to
      // heap 0.
      getMainHoardHeap()->chooseZero();
    } else {
      getMainHoardHeap()->findUnusedHeap();
    }
    getCustomHeap();
    break;

  case DLL_THREAD_DETACH:
    {
      // Dump the memory from the TLAB.
      getCustomHeap()->clear();

      TheCustomHeapType *heap
	= (TheCustomHeapType *) TlsGetValue(LocalTLABIndex);

      if (np != 1) {
	// If we're on a multiprocessor box, relinquish the heap
	// assigned to this thread.
	getMainHoardHeap()->releaseHeap();
      }

      if (heap != 0) {
	TlsSetValue (LocalTLABIndex, 0);
      }
    }
    break;

  case DLL_PROCESS_DETACH:
    HOARD_POST_ACTION;
    break;

  default:
    return TRUE;
  }

  return TRUE;
}

}

#else

extern "C" {

typedef void * (*threadFunctionType) (void *);

typedef
int (*pthread_create_function) (pthread_t *thread,
				const pthread_attr_t *attr,
				threadFunctionType start_routine,
				void *arg);
}

// A special routine we call on thread exits to free up some resources.
static void exitRoutine (void) {
  // Clear the TLAB's buffer.
  getCustomHeap()->clear();

  // Relinquish the assigned heap.
  getMainHoardHeap()->releaseHeap();
}


extern "C" {

static void * startMeUp (void * a)
{
  getCustomHeap();
  getMainHoardHeap()->findUnusedHeap();

  pair<threadFunctionType, void *> * z
    = (pair<threadFunctionType, void *> *) a;

  threadFunctionType f = z->first;
  void * arg = z->second;
  void * result = (*f)(arg);
  exitRoutine();
  delete z;
  return result;
}

}

#include <dlfcn.h>


extern "C" int pthread_create (pthread_t *thread,
			       const pthread_attr_t *attr,
			       void * (*start_routine) (void *),
			       void * arg)
#if !defined(__SUNPRO_CC)
  throw ()
#endif
{
  // A pointer to the library version of pthread_create.
  static pthread_create_function real_pthread_create = NULL;

  // Force initialization of the TLAB before our first thread is created.
  volatile static TheCustomHeapType * t = getCustomHeap();

#if defined(linux)
  char fname[] = "pthread_create";
#else
  char fname[] = "_pthread_create";
#endif

  // Instantiate the pointer to pthread_create, if it hasn't been
  // instantiated yet.

  if (real_pthread_create == NULL) {
    real_pthread_create = (pthread_create_function) dlsym (RTLD_NEXT, fname);
    if (real_pthread_create == NULL) {
      fprintf (stderr, "Could not find the pthread_create function!\n");
      fprintf (stderr, "Please report this problem to emery@cs.umass.edu.\n");
      abort();
    }
  }

  anyThreadCreated = 1;

  pair<threadFunctionType, void *> * args =
    new pair<threadFunctionType, void *> (start_routine, arg);

  int result = (*real_pthread_create)(thread, attr, startMeUp, args);

  return result;
}

#endif
