// -*- C++ -*-

#ifndef _HOARDSUPERBLOCKHEADER_H_
#define _HOARDSUPERBLOCKHEADER_H_

#include <stdlib.h>

namespace Hoard {

template <class LockType,
	  int SuperblockSize,
	  typename HeapType>
class HoardSuperblock;

template <class LockType,
	  int SuperblockSize,
	  typename HeapType>
class HoardSuperblockHeader {
public:

  HoardSuperblockHeader (size_t sz, size_t bufferSize)
    : magicNumber (MAGIC_NUMBER),
      objectSize (sz),
      objectSizeIsPowerOfTwo (!(sz & (sz - 1)) && sz),
      totalObjects (bufferSize / sz),
      owner (NULL),
      prev (NULL),
      next (NULL),
      reapableObjects (bufferSize / sz),
      objectsFree (bufferSize / sz),
      start ((char *) (this + 1)),
      position (start)
  {
  }
  
  inline void * malloc (void) {
    assert (isValid());
    void * ptr = reapAlloc();
    if (!ptr) {
      ptr = freeListAlloc();
    }
    assert ((size_t) ptr % sizeof(double) == 0);
    return ptr;
  }

  inline void free (void * ptr) {
    assert (isValid());
    freeList.insert (reinterpret_cast<FreeSLList::Entry *>(ptr));
    objectsFree++;
    if (objectsFree == totalObjects) {
      clear();
    }
  }

  void clear (void) {
    assert (isValid());
    // Clear out the freelist.
    freeList.clear();
    // All the objects are now free.
    objectsFree = totalObjects;
    reapableObjects = totalObjects;
    position = start;
  }

  /// @brief Returns the actual start of the object.
  INLINE void * normalize (void * ptr) const {
    assert (isValid());
    size_t offset = (size_t) ptr - (size_t) start;
    void * p;

    // Optimization note: the modulo operation (%) is *really* slow on
    // some architectures (notably x86-64). To reduce its overhead, we
    // optimize for the case when the size request is a power of two,
    // which is often enough to make a difference.

    if (objectSizeIsPowerOfTwo) {
      p = (void *) ((size_t) ptr - (offset & (objectSize - 1)));
    } else {
      p = (void *) ((size_t) ptr - (offset % objectSize));
    }
    return p;
  }


  size_t getSize (void * ptr) const {
    assert (isValid());
    size_t offset = (size_t) ptr - (size_t) start;
    size_t newSize;
    if (objectSizeIsPowerOfTwo) {
      newSize = objectSize - (offset & (objectSize - 1));
    } else {
      newSize = objectSize - (offset % objectSize);
    }
    return newSize;
  }

  size_t getObjectSize (void) const {
    return objectSize;
  }

  int getTotalObjects (void) const {
    return totalObjects;
  }

  int getObjectsFree (void) const {
    return objectsFree;
  }

  HeapType * getOwner (void) const {
    return owner;
  }

  void setOwner (HeapType * o) {
    owner = o;
  }

  bool isValid (void) const {
    return (magicNumber == MAGIC_NUMBER);
  }

  HoardSuperblock<LockType, SuperblockSize, HeapType> * getNext (void) const {
    return next;
  }

  HoardSuperblock<LockType, SuperblockSize, HeapType> * getPrev (void) const {
    return prev;
  }

  void setNext (HoardSuperblock<LockType, SuperblockSize, HeapType> * n) {
    next = n;
  }

  void setPrev (HoardSuperblock<LockType, SuperblockSize, HeapType> * p) {
    prev = p;
  }

  void lock (void) {
    theLock.lock();
  }

  void unlock (void) {
    theLock.unlock();
  }

private:

  MALLOC_FUNCTION INLINE void * reapAlloc (void) {
    assert (isValid());
    assert (position);
    // Reap mode.
    if (reapableObjects > 0) {
      char * ptr = position;
      position = ptr + objectSize;
      reapableObjects--;
      objectsFree--;
      return ptr;
    } else {
      return NULL;
    }
  }

  MALLOC_FUNCTION INLINE void * freeListAlloc (void) {
    assert (isValid());
    // Freelist mode.
    char * ptr = reinterpret_cast<char *>(freeList.get());
    if (ptr) {
      assert (objectsFree >= 1);
      objectsFree--;
    }
    return ptr;
  }

  enum { MAGIC_NUMBER = 0xcafed00d };

  const size_t magicNumber;

  /// The object size.
  const size_t objectSize;

  const bool objectSizeIsPowerOfTwo;

  /// Total objects in the superblock.
  const int totalObjects;

  /// The lock.
  LockType theLock;

  /// The owner of this superblock.
  HeapType * owner;

  /// The preceding superblock in a linked list.
  HoardSuperblock<LockType, SuperblockSize, HeapType> * prev;

  /// The succeeding superblock in a linked list.
  HoardSuperblock<LockType, SuperblockSize, HeapType> * next;
    
  /// The number of objects available to be 'reap'ed.
  int reapableObjects;

  /// The number of objects available for (re)use.
  int objectsFree;

  /// The start of reap allocation.
  char * start;

  /// The list of freed objects.
  FreeSLList freeList;

  /// The cursor into the buffer following the header.
  char * position;

private:

  double _dummy; // for alignment
};

};

#endif
