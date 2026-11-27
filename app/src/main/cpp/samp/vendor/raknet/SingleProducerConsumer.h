/// \file
/// \brief [Internal] Passes queued data between threads using a circular buffer with read and write pointers
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
/// Software Foundation; either version 2 of the License, or
/// (at your option) any later version.

#ifndef __SINGLE_PRODUCER_CONSUMER_H
#define __SINGLE_PRODUCER_CONSUMER_H

#include <assert.h>
#include <atomic>
#include <string.h>

static const int MINIMUM_LIST_SIZE=8;

#include "Export.h"

/// The namespace DataStructures was only added to avoid compiler errors for commonly named data structures
/// As these data structures are stand-alone, you can use them outside of RakNet for your own projects if you wish.
namespace DataStructures
{
	/// \brief A single producer consumer implementation without critical sections.
	///
	/// Correcao importante (SA-MP Mobile / ARM64): a versao original usava
	/// "volatile" para a flag readyToRead e nao inicializava os nos criados com
	/// "new DataPlusPtr". Em ARM64 (memoria fraca) o consumidor podia observar
	/// readyToRead == true e ler o ponteiro do pacote ainda nao publicado pelo
	/// produtor (ou lixo da heap). Isso derrubava o jogo em
	/// RakPeer::ReceiveIgnoreRPC depois de alguns minutos de jogo.
	/// Agora os ponteiros e a flag sao atomicos (acquire/release).
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
			SingleProducerConsumerType object;

			// Ready to read is so we can use an equality boolean comparison, in case the writePointer var is trashed while context switching.
			std::atomic<bool> readyToRead;
			std::atomic<DataPlusPtr*> next;

			// Os nos eram criados sem inicializacao: readyToRead ficava com lixo e
			// o consumidor podia ler um bloco que o produtor ainda nao escreveu.
			DataPlusPtr() : readyToRead(false), next(0)
			{
				memset(&object, 0, sizeof(object));
			}
		};

		std::atomic<DataPlusPtr*> readAheadPointer;
		std::atomic<DataPlusPtr*> writeAheadPointer;
		std::atomic<DataPlusPtr*> readPointer;
		std::atomic<DataPlusPtr*> writePointer;
		std::atomic<unsigned> readCount, writeCount;
	};

	template <class SingleProducerConsumerType>
		SingleProducerConsumer<SingleProducerConsumerType>::SingleProducerConsumer()
	{
		// Preallocate
		DataPlusPtr* start = new DataPlusPtr;
		readPointer.store(start, std::memory_order_relaxed);
		writePointer.store(start, std::memory_order_relaxed);

		DataPlusPtr* cursor = start;
		cursor->next.store(new DataPlusPtr, std::memory_order_relaxed);
		int listSize;
#ifdef _DEBUG
		assert(MINIMUM_LIST_SIZE>=3);
#endif
		for (listSize=2; listSize < MINIMUM_LIST_SIZE; listSize++)
		{
			cursor = cursor->next.load(std::memory_order_relaxed);
			cursor->next.store(new DataPlusPtr, std::memory_order_relaxed);
		}
		cursor->next.load(std::memory_order_relaxed)->next.store(start, std::memory_order_relaxed); // last to next = start

		readAheadPointer.store(start, std::memory_order_relaxed);
		writeAheadPointer.store(start, std::memory_order_relaxed);
		readCount.store(0, std::memory_order_relaxed);
		writeCount.store(0, std::memory_order_relaxed);
	}

	template <class SingleProducerConsumerType>
		SingleProducerConsumer<SingleProducerConsumerType>::~SingleProducerConsumer()
	{
		DataPlusPtr *next, *cursor;
		cursor = writeAheadPointer.load(std::memory_order_relaxed)->next.load(std::memory_order_relaxed);
		while (cursor != writeAheadPointer.load(std::memory_order_relaxed))
		{
			next = cursor->next.load(std::memory_order_relaxed);
			delete (char*) cursor;
			cursor = next;
		}
		delete (char*) cursor;
	}

	template <class SingleProducerConsumerType>
		SingleProducerConsumerType* SingleProducerConsumer<SingleProducerConsumerType>::WriteLock( void )
	{
		DataPlusPtr* ahead = writeAheadPointer.load(std::memory_order_relaxed);
		DataPlusPtr* aheadNext = ahead->next.load(std::memory_order_acquire);

		if (aheadNext == readPointer.load(std::memory_order_acquire) ||
			aheadNext->readyToRead.load(std::memory_order_acquire) == true)
		{
			DataPlusPtr* originalNext = aheadNext;
			DataPlusPtr* newNode = new DataPlusPtr;
			assert(newNode);
			newNode->next.store(originalNext, std::memory_order_relaxed);
			ahead->next.store(newNode, std::memory_order_release);
			aheadNext = newNode;
		}

		writeAheadPointer.store(aheadNext, std::memory_order_relaxed);

		return (SingleProducerConsumerType*) ahead;
	}

	template <class SingleProducerConsumerType>
		void SingleProducerConsumer<SingleProducerConsumerType>::CancelWriteLock( SingleProducerConsumerType* cancelToLocation )
	{
		writeAheadPointer.store((DataPlusPtr *)cancelToLocation, std::memory_order_relaxed);
	}

	template <class SingleProducerConsumerType>
		void SingleProducerConsumer<SingleProducerConsumerType>::WriteUnlock( void )
	{
#ifdef _DEBUG
		assert(writePointer.load(std::memory_order_relaxed)->next.load(std::memory_order_relaxed)!=readPointer.load(std::memory_order_relaxed));
		assert(writePointer.load(std::memory_order_relaxed)!=writeAheadPointer.load(std::memory_order_relaxed));
#endif

		writeCount.fetch_add(1, std::memory_order_relaxed);

		// User is done with the data, allow send by updating the write pointer.
		// Release: tudo que foi escrito em "object" fica visivel para o leitor
		// que observar readyToRead == true (acquire em ReadLock).
		DataPlusPtr* wp = writePointer.load(std::memory_order_relaxed);
		wp->readyToRead.store(true, std::memory_order_release);
		writePointer.store(wp->next.load(std::memory_order_acquire), std::memory_order_relaxed);
	}

	template <class SingleProducerConsumerType>
		SingleProducerConsumerType* SingleProducerConsumer<SingleProducerConsumerType>::ReadLock( void )
	{
		DataPlusPtr* ra = readAheadPointer.load(std::memory_order_relaxed);

		if (ra == writePointer.load(std::memory_order_acquire) ||
			ra->readyToRead.load(std::memory_order_acquire) == false)
		{
			return 0;
		}

		readAheadPointer.store(ra->next.load(std::memory_order_relaxed), std::memory_order_relaxed);
		return (SingleProducerConsumerType*)ra;
	}

	template <class SingleProducerConsumerType>
		void SingleProducerConsumer<SingleProducerConsumerType>::CancelReadLock( SingleProducerConsumerType* cancelToLocation )
	{
#ifdef _DEBUG
		assert(readPointer.load(std::memory_order_relaxed)!=writePointer.load(std::memory_order_relaxed));
#endif
		readAheadPointer.store((DataPlusPtr *)cancelToLocation, std::memory_order_relaxed);
	}

	template <class SingleProducerConsumerType>
		void SingleProducerConsumer<SingleProducerConsumerType>::ReadUnlock( void )
	{
#ifdef _DEBUG
		assert(readAheadPointer.load(std::memory_order_relaxed)!=readPointer.load(std::memory_order_relaxed)); // If hits, then called ReadUnlock before ReadLock
		assert(readPointer.load(std::memory_order_relaxed)!=writePointer.load(std::memory_order_relaxed)); // If hits, then called ReadUnlock when Read returns 0
#endif
		readCount.fetch_add(1, std::memory_order_relaxed);

		// Allow writes to this memory block
		DataPlusPtr* rp = readPointer.load(std::memory_order_relaxed);
		rp->readyToRead.store(false, std::memory_order_release);
		readPointer.store(rp->next.load(std::memory_order_acquire), std::memory_order_relaxed);
	}

	template <class SingleProducerConsumerType>
		void SingleProducerConsumer<SingleProducerConsumerType>::Clear( void )
	{
		// Shrink the list down to MINIMUM_LIST_SIZE elements
		DataPlusPtr* readPtr = readPointer.load(std::memory_order_relaxed);
		DataPlusPtr* writePtr = readPtr->next.load(std::memory_order_relaxed);
		writePointer.store(writePtr, std::memory_order_relaxed);

		int listSize=1;
		DataPlusPtr* next = readPtr->next.load(std::memory_order_relaxed);
		while (next != readPtr)
		{
			listSize++;
			next = next->next.load(std::memory_order_relaxed);
		}

		while (listSize-- > MINIMUM_LIST_SIZE)
		{
			next = writePointer.load(std::memory_order_relaxed)->next.load(std::memory_order_relaxed);
#ifdef _DEBUG
			assert(writePointer.load(std::memory_order_relaxed)!=readPtr);
#endif
			delete (char*) writePointer.load(std::memory_order_relaxed);
			writePointer.store(next, std::memory_order_relaxed);
		}

		readPtr->next.store(writePointer.load(std::memory_order_relaxed), std::memory_order_release);
		writePointer.store(readPtr, std::memory_order_relaxed);
		readAheadPointer.store(readPtr, std::memory_order_relaxed);
		writeAheadPointer.store(readPtr, std::memory_order_relaxed);
		readCount.store(0, std::memory_order_relaxed);
		writeCount.store(0, std::memory_order_relaxed);
	}

	template <class SingleProducerConsumerType>
		int SingleProducerConsumer<SingleProducerConsumerType>::Size( void ) const
	{
		return (int)(writeCount.load(std::memory_order_relaxed) - readCount.load(std::memory_order_relaxed));
	}

	template <class SingleProducerConsumerType>
		bool SingleProducerConsumer<SingleProducerConsumerType>::CheckReadUnlockOrder(const SingleProducerConsumerType* data) const
	{
		return &readPointer.load(std::memory_order_relaxed)->object == data;
	}

	template <class SingleProducerConsumerType>
		bool SingleProducerConsumer<SingleProducerConsumerType>::ReadIsLocked(void) const
	{
		return readAheadPointer.load(std::memory_order_relaxed) != readPointer.load(std::memory_order_relaxed);
	}
}

#endif
