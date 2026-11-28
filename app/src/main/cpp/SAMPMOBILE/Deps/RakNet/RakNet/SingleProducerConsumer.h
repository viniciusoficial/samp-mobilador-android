/// \file
/// \brief \b [Internal] Passes queued data between threads using a circular buffer with read and write pointers
///
/// This file is part of RakNet Copyright 2003 Kevin Jenkins.
///
/// Usage of RakNet is subject to the appropriate license agreement.
/// Creative Commons Licensees are subject to the
/// license found at
/// http://creativecommons.org/licenses/by-nc/2.5/
/// Single application licensees are subject to the license found at
/// http://www.rakkarsoft.com/SingleApplicationLicense.html
/// Custom license users are subject to the terms therein.
/// GPL license users are subject to the GNU General Public
/// License as published by the Free
/// Software Foundation; either version 2 of the License, or (at your
/// option) any later version.

#ifndef __SINGLE_PRODUCER_CONSUMER_H
#define __SINGLE_PRODUCER_CONSUMER_H

#include <assert.h>
#include <atomic>

static const int MINIMUM_LIST_SIZE=8;

#include "Export.h"

/// The namespace DataStructures was only added to avoid compiler errors for commonly named data structures
/// As these data structures are stand-alone, you can use them outside of RakNet for your own projects if you wish.
namespace DataStructures
{
	/// \brief A single producer consumer implementation without critical sections.
	template <class SingleProducerConsumerType>
	class RAK_DLL_EXPORT SingleProducerConsumer
	{
	public:
		/// Constructor
		SingleProducerConsumer();

		/// Destructor
		~SingleProducerConsumer();

		/// WriteLock must be immediately followed by WriteUnlock.  These two functions must be called in the same thread.
		/// \return A pointer to a block of data you can write to.
		SingleProducerConsumerType* WriteLock(void);

		/// Call if you don't want to write to a block of data from WriteLock() after all.
		/// Cancelling locks cancels all locks back up to the data passed.  So if you lock twice and cancel using the first lock, the second lock is ignored
		/// \param[in] cancelToLocation Which WriteLock() to cancel.
		void CancelWriteLock(SingleProducerConsumerType* cancelToLocation);

		/// Call when you are done writing to a block of memory returned by WriteLock()
		void WriteUnlock(void);

		/// ReadLock must be immediately followed by ReadUnlock. These two functions must be called in the same thread.
		/// \retval 0 No data is availble to read
		/// \retval Non-zero The data previously written to, in another thread, by WriteLock followed by WriteUnlock.
		SingleProducerConsumerType* ReadLock(void);

		// Cancelling locks cancels all locks back up to the data passed.  So if you lock twice and cancel using the first lock, the second lock is ignored
		/// param[in] Which ReadLock() to cancel.
		void CancelReadLock(SingleProducerConsumerType* cancelToLocation);

		/// Signals that we are done reading the the data from the least recent call of ReadLock.
		/// At this point that pointer is no longer valid, and should no longer be read.
		void ReadUnlock(void);

		/// Clear is not thread-safe and none of the lock or unlock functions should be called while it is running.
		void Clear(void);

		/// This function will estimate how many elements are waiting to be read.  It's threadsafe enough that the value returned is stable, but not threadsafe enough to give accurate results.
		/// \return An ESTIMATE of how many data elements are waiting to be read
		int Size(void) const;

		/// Make sure that the pointer we done reading for the call to ReadUnlock is the right pointer.
		/// param[in] A previous pointer returned by ReadLock()
		bool CheckReadUnlockOrder(const SingleProducerConsumerType* data) const;

		/// Returns if ReadUnlock was called before ReadLock
		/// \return If the read is locked
		bool ReadIsLocked(void) const;

	private:
		struct DataPlusPtr
		{
			SingleProducerConsumerType object{};
			std::atomic<bool> readyToRead{false};
			std::atomic<DataPlusPtr*> next{nullptr};
		};
		DataPlusPtr *readAheadPointer = nullptr;
		DataPlusPtr *writeAheadPointer = nullptr;
		std::atomic<DataPlusPtr*> readPointer{nullptr};
		std::atomic<DataPlusPtr*> writePointer{nullptr};
		std::atomic<unsigned> readCount{0}, writeCount{0};
	};

	template <class SingleProducerConsumerType>
		SingleProducerConsumer<SingleProducerConsumerType>::SingleProducerConsumer()
	{
		DataPlusPtr *first = new DataPlusPtr();
		DataPlusPtr *cur = first;
		for (int listSize = 1; listSize < MINIMUM_LIST_SIZE; listSize++)
		{
			DataPlusPtr *nxt = new DataPlusPtr();
			cur->next.store(nxt, std::memory_order_relaxed);
			cur = nxt;
		}
		cur->next.store(first, std::memory_order_relaxed);
		readPointer.store(first, std::memory_order_relaxed);
		writePointer.store(first, std::memory_order_relaxed);
		readAheadPointer = first;
		writeAheadPointer = first;
		readCount.store(0, std::memory_order_relaxed);
		writeCount.store(0, std::memory_order_relaxed);
	}

	template <class SingleProducerConsumerType>
		SingleProducerConsumer<SingleProducerConsumerType>::~SingleProducerConsumer()
	{
		DataPlusPtr *stop = writeAheadPointer;
		if (!stop) return;
		DataPlusPtr *cur = stop->next.load(std::memory_order_relaxed);
		while (cur && cur != stop)
		{
			DataPlusPtr *nxt = cur->next.load(std::memory_order_relaxed);
			delete cur;
			cur = nxt;
		}
		delete stop;
	}

	template <class SingleProducerConsumerType>
		SingleProducerConsumerType* SingleProducerConsumer<SingleProducerConsumerType>::WriteLock( void )
	{
		DataPlusPtr *nextNode = writeAheadPointer->next.load(std::memory_order_acquire);
		if (nextNode == readPointer.load(std::memory_order_acquire) ||
			nextNode->readyToRead.load(std::memory_order_acquire))
		{
			DataPlusPtr *inserted = new DataPlusPtr();
			inserted->next.store(nextNode, std::memory_order_relaxed);
			writeAheadPointer->next.store(inserted, std::memory_order_release);
			nextNode = inserted;
		}

		DataPlusPtr *last = writeAheadPointer;
		writeAheadPointer = nextNode;

		return &last->object;
	}

	template <class SingleProducerConsumerType>
		void SingleProducerConsumer<SingleProducerConsumerType>::CancelWriteLock( SingleProducerConsumerType* cancelToLocation )
	{
		writeAheadPointer = reinterpret_cast<DataPlusPtr*>(cancelToLocation);
	}

	template <class SingleProducerConsumerType>
		void SingleProducerConsumer<SingleProducerConsumerType>::WriteUnlock( void )
	{
		DataPlusPtr *cur = writePointer.load(std::memory_order_relaxed);
#ifdef _DEBUG
		assert(cur->next.load(std::memory_order_acquire) != readPointer.load(std::memory_order_acquire));
		assert(cur != writeAheadPointer);
#endif
		DataPlusPtr *nxt = cur->next.load(std::memory_order_acquire);
		writeCount.fetch_add(1, std::memory_order_relaxed);
		cur->readyToRead.store(true, std::memory_order_release);
		writePointer.store(nxt, std::memory_order_release);
	}

	template <class SingleProducerConsumerType>
		SingleProducerConsumerType* SingleProducerConsumer<SingleProducerConsumerType>::ReadLock( void )
	{
		if (readAheadPointer == writePointer.load(std::memory_order_acquire) ||
			!readAheadPointer->readyToRead.load(std::memory_order_acquire))
		{
			return 0;
		}

		DataPlusPtr *last = readAheadPointer;
		readAheadPointer = readAheadPointer->next.load(std::memory_order_acquire);
		return &last->object;
	}

	template <class SingleProducerConsumerType>
		void SingleProducerConsumer<SingleProducerConsumerType>::CancelReadLock( SingleProducerConsumerType* cancelToLocation )
	{
#ifdef _DEBUG
		assert(readPointer.load(std::memory_order_acquire) != writePointer.load(std::memory_order_acquire));
#endif
		readAheadPointer = reinterpret_cast<DataPlusPtr*>(cancelToLocation);
	}

	template <class SingleProducerConsumerType>
		void SingleProducerConsumer<SingleProducerConsumerType>::ReadUnlock( void )
	{
		DataPlusPtr *cur = readPointer.load(std::memory_order_relaxed);
#ifdef _DEBUG
		assert(readAheadPointer != cur);
		assert(cur != writePointer.load(std::memory_order_acquire));
#endif
		DataPlusPtr *nxt = cur->next.load(std::memory_order_acquire);
		readCount.fetch_add(1, std::memory_order_relaxed);
		cur->readyToRead.store(false, std::memory_order_release);
		readPointer.store(nxt, std::memory_order_release);
	}

	template <class SingleProducerConsumerType>
		void SingleProducerConsumer<SingleProducerConsumerType>::Clear( void )
	{
		DataPlusPtr *rp = readPointer.load(std::memory_order_relaxed);
		if (!rp) return;
		DataPlusPtr *cur = rp->next.load(std::memory_order_relaxed);

		int listSize = 1;
		DataPlusPtr *scan = cur;
		while (scan != rp)
		{
			listSize++;
			scan->readyToRead.store(false, std::memory_order_relaxed);
			scan = scan->next.load(std::memory_order_relaxed);
		}
		rp->readyToRead.store(false, std::memory_order_relaxed);

		while (listSize-- > MINIMUM_LIST_SIZE)
		{
			DataPlusPtr *nxt = cur->next.load(std::memory_order_relaxed);
			delete cur;
			cur = nxt;
		}

		rp->next.store(cur, std::memory_order_relaxed);
		writePointer.store(rp, std::memory_order_release);
		readAheadPointer = rp;
		writeAheadPointer = rp;
		readCount.store(0, std::memory_order_relaxed);
		writeCount.store(0, std::memory_order_relaxed);
	}

	template <class SingleProducerConsumerType>
		int SingleProducerConsumer<SingleProducerConsumerType>::Size( void ) const
	{
		return static_cast<int>(writeCount.load(std::memory_order_relaxed) - readCount.load(std::memory_order_relaxed));
	}

	template <class SingleProducerConsumerType>
		bool SingleProducerConsumer<SingleProducerConsumerType>::CheckReadUnlockOrder(const SingleProducerConsumerType* data) const
	{
		DataPlusPtr *rp = readPointer.load(std::memory_order_relaxed);
		return rp && (&rp->object == data);
	}


	template <class SingleProducerConsumerType>
		bool SingleProducerConsumer<SingleProducerConsumerType>::ReadIsLocked(void) const
	{
		return readAheadPointer != readPointer.load(std::memory_order_relaxed);
	}	
}

#endif

/*
#include "SingleProducerConsumer.h"
#include <process.h>
#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include <math.h>
#include <stdlib.h>

#define READ_COUNT_ITERATIONS 10000000

DataStructures::SingleProducerConsumer<unsigned long> spc;
unsigned long readCount;

unsigned __stdcall ProducerThread( LPVOID arguments )
{
unsigned long producerCount;
unsigned long *writeBlock;
producerCount=0;
while (readCount < READ_COUNT_ITERATIONS)
{
writeBlock=spc.WriteLock();
*writeBlock=producerCount;
spc.WriteUnlock();
producerCount++;
if ((producerCount%1000000)==0)
{
printf("WriteCount: %i. BufferSize=%i\n", producerCount, spc.Size());
}
}
printf("PRODUCER THREAD ENDED!\n");
return 0;
}

unsigned __stdcall ConsumerThread( LPVOID arguments )
{
unsigned long *readBlock;
while (readCount < READ_COUNT_ITERATIONS)
{
if ((readBlock=spc.ReadLock())!=0)
{
if (*readBlock!=readCount)
{
printf("Test failed! Expected %i got %i!\n", readCount, *readBlock);
readCount = READ_COUNT_ITERATIONS;
assert(0);
}
spc.ReadUnlock();
readCount++;
if ((readCount%1000000)==0)
{
printf("ReadCount:  %i. BufferSize=%i\n", readCount, spc.Size());
}
}
}
printf("CONSUMER THREAD ENDED!\n");
return 0;
}

void main(void)
{
readCount=0;
unsigned threadId1 = 0;
unsigned threadId2 = 0;
HANDLE thread1Handle, thread2Handle;
unsigned long startTime = timeGetTime();

thread1Handle=(HANDLE)_beginthreadex( NULL, 0, ProducerThread, 0, 0, &threadId1 );
thread2Handle=(HANDLE)_beginthreadex( NULL, 0, ConsumerThread, 0, 0, &threadId1 );

while (readCount < READ_COUNT_ITERATIONS)
{
Sleep(0);
}
char str[256];
printf("Elapsed time = %i milliseconds. Press Enter to continue\n", timeGetTime() - startTime);
gets(str);
}
*/
