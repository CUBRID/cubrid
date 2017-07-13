/* -*- C++ -*- */

#ifndef _PHOThreadHeap_H_
#define _PHOThreadHeap_H_

#include <assert.h>

static /*volatile*/ int getThreadId (void);

template <int NumHeaps, class super>
class MarkThreadHeap : public super {
public:

  inline void * malloc (size_t sz) {
    int tid = getThreadId() % NumHeaps;
    void * ptr = super::malloc (sz);
    if (ptr != NULL) {
      super::setHeap(ptr, tid);
      super::setPrevHeap(super::getNext(ptr), tid);
    }
    return ptr;
  }
};


template <int NumHeaps, class super>
class CheckThreadHeap : public super {
public:

  inline void * malloc (size_t sz) {
    int tid = getThreadId() % NumHeaps;
    void * ptr = super::malloc (sz);
    if (ptr != NULL)
      assert (super::getHeap(ptr) == tid);
    return ptr;
  }

  inline void free (void * ptr) {
    super::free (ptr);
  }
};



/*

A PHOThreadHeap comprises NumHeaps "per-thread" heaps.

To pick a per-thread heap, the current thread id is hashed (mod NumHeaps).

malloc gets memory from its hashed per-thread heap.
free returns memory to its originating heap.

NB: We assume that the thread heaps are 'locked' as needed.  */


template <int NumHeaps, class super>
class PHOThreadHeap { // : public MarkThreadHeap<NumHeaps, super> {
public:

  inline void * malloc (size_t sz) {
    int tid = getThreadId() % NumHeaps;
    void * ptr = selectHeap(tid)->malloc (sz);
    return ptr;
  }

  inline void free (void * ptr) {
    int tid = super::getHeap(ptr);
    selectHeap(tid)->free (ptr);
  }


  inline int remove (void * ptr);
#if 0
  {
    int tid = super::getHeap(ptr);
    selectHeap(tid)->remove (ptr);
  }
#endif

private:

  // Access the given heap within the buffer.
  MarkThreadHeap<NumHeaps, super> * selectHeap (int index) {
    assert (index >= 0);
    assert (index < NumHeaps);
    return &ptHeaps[index];
  }

  MarkThreadHeap<NumHeaps, super> ptHeaps[NumHeaps];

};


// A platform-dependent way to get a thread id.

// Include the necessary platform-dependent crud.
#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
#ifndef WIN32
#define WIN32 1
#endif
#include <windows.h>
#include <process.h>
#endif

#if defined(__SVR4)
extern "C" unsigned int lwp_self (void);
#endif


static /*volatile*/ int getThreadId (void) {
#if defined(WIN32)
  // It looks like thread id's are always multiples of 4, so...
  int tid = GetCurrentThreadId() >> 2;
  // Now hash in some of the first bits.
  //  return tid;
  return (tid & ~(1024-1)) ^ tid;
#endif
#if defined(__BEOS__)
  return find_thread(0);
#endif
#if defined(__linux)
  // Consecutive thread id's in Linux are 1024 apart;
  // dividing off the 1024 gives us an appropriate thread id.
  return (int) pthread_self() >> 10; // (>> 10 = / 1024)
#endif
#if defined(__SVR4)
  return (int) lwp_self();
#endif
#if defined(POSIX)
  return (int) pthread_self();
#endif
#if USE_SPROC
  // This hairiness has the same effect as calling getpid(),
  // but it's MUCH faster since it avoids making a system call
  // and just accesses the sproc-local data directly.
  int pid = (int) PRDA->sys_prda.prda_sys.t_pid;
  return pid;
#endif
}
  

#endif
