#include "xallocator.h"
#include "allocator.h"
#include "Fault.h"
#include <iostream>

using namespace std;

#ifndef CHAR_BIT
#define CHAR_BIT	8 
#endif

static CRITICAL_SECTION _criticalSection; 
static BOOL _xallocInitialized = FALSE;

// Define STATIC_POOLS to switch from heap blocks mode to static pools mode
//#define STATIC_POOLS 
#ifdef STATIC_POOLS
	// Update this section as necessary if you want to use static memory pools
	#define MAX_ALLOCATORS	11
	#define MAX_BLOCKS		32

	AllocatorPool<char[8], MAX_BLOCKS> allocator8;
	AllocatorPool<char[16], MAX_BLOCKS> allocator16;
	AllocatorPool<char[32], MAX_BLOCKS> allocator32;
	AllocatorPool<char[64], MAX_BLOCKS> allocator64;
	AllocatorPool<char[128], MAX_BLOCKS> allocator128;
	AllocatorPool<char[256], MAX_BLOCKS> allocator256;
	AllocatorPool<char[396], MAX_BLOCKS> allocator396;
	AllocatorPool<char[512], MAX_BLOCKS> allocator512;
	AllocatorPool<char[768], MAX_BLOCKS> allocator768;
	AllocatorPool<char[1024], MAX_BLOCKS> allocator1024;
	AllocatorPool<char[2048], MAX_BLOCKS> allocator2048;

	static Allocator* _allocators[MAX_ALLOCATORS] = {
		&allocator8,
		&allocator16,
		&allocator32,
		&allocator64,
		&allocator128,
		&allocator256,
		&allocator396,
		&allocator512,
		&allocator768,
		&allocator1024,
		&allocator2048,
	};
#else
	#define MAX_ALLOCATORS  15
	static Allocator* _allocators[MAX_ALLOCATORS];
#endif	// STATIC_POOLS

#ifdef AUTOMATIC_XALLOCATOR_INIT_DESTROY
INT XallocInitDestroy::refCount = 0;
XallocInitDestroy::XallocInitDestroy() 
{ 
	// Track how many static instances of XallocInitDestroy are created
	if (refCount++ == 0)
		xalloc_init();
}

XallocInitDestroy::~XallocInitDestroy()
{
	// Last static instance to have destructor called?
	if (--refCount == 0)
		xalloc_destroy();
}
#endif	// AUTOMATIC_XALLOCATOR_INIT_DESTROY

/// Returns the next higher powers of two. For instance, pass in 12 and 
/// the value returned would be 16. 
/// @param[in] k - numeric value to compute the next higher power of two.
/// @return	The next higher power of two based on the input k. 
template <class T>
T nexthigher(T k) 
{
	if (k == 0)
		return 1;
	k--;
	for (unsigned int i=1; i<sizeof(T)*CHAR_BIT; i<<=1)
		k = k | k >> i;
	return k+1;
}

/// Create the xallocator lock. Call only one time at startup. 
static void lock_init()
{
	BOOL success = InitializeCriticalSectionAndSpinCount(&_criticalSection, 0x00000400);
	ASSERT_TRUE(success != 0);
	_xallocInitialized = TRUE;
}

/// Destroy the xallocator lock.
static void lock_destroy()
{
	DeleteCriticalSection(&_criticalSection);
	_xallocInitialized = FALSE;
}

/// Lock the shared resource. 
static inline void lock_get()
{
	if (_xallocInitialized == FALSE)
		return;

	EnterCriticalSection(&_criticalSection); 
}

/// Unlock the shared resource. 
static inline void lock_release()
{
	if (_xallocInitialized == FALSE)
		return;

	LeaveCriticalSection(&_criticalSection);
}

/// Stored a pointer to the allocator instance within the block region. 
///	a pointer to the client's area within the block.
/// @param[in] block - a pointer to the raw memory block. 
///	@param[in] size - the client requested size of the memory block.
/// @return	A pointer to the client's address within the raw memory block. 
static inline void *set_block_allocator(void* block, Allocator* allocator)
{
	// Cast the raw block memory to a Allocator pointer
	Allocator** pAllocatorInBlock = static_cast<Allocator**>(block);

	// Write the size into the memory block
	*pAllocatorInBlock = allocator;

	// Advance the pointer past the Allocator* block size and return a pointer to
	// the client's memory region
	return ++pAllocatorInBlock;
}

/// Gets the size of the memory block stored within the block.
/// @param[in] block - a pointer to the client's memory block. 
/// @return	The original allocator instance stored in the memory block.
static inline Allocator* get_block_allocator(void* block)
{
	// Cast the client memory to a Allocator pointer
	Allocator** pAllocatorInBlock = static_cast<Allocator**>(block);

	// Back up one Allocator* position to get the stored allocator instance
	pAllocatorInBlock--;

	// Return the allocator instance stored within the memory block
	return *pAllocatorInBlock;
}

/// Returns the raw memory block pointer given a client memory pointer. 
/// @param[in] block - a pointer to the client memory block. 
/// @return	A pointer to the original raw memory block address. 
static inline void *get_block_ptr(void* block)
{
	// Cast the client memory to a Allocator* pointer
	Allocator** pAllocatorInBlock = static_cast<Allocator**>(block);

	// Back up one Allocator* position and return the original raw memory block pointer
	return --pAllocatorInBlock;
}

/// Returns an allocator instance matching the size provided
/// @param[in] size - allocator block size
/// @return Allocator instance handling requested block size or NULL
/// if no allocator exists. 
static inline Allocator* find_allocator(size_t size)
{
	for (INT i=0; i<MAX_ALLOCATORS; i++)
	{
		if (_allocators[i] == 0)
			break;
		
		if (_allocators[i]->GetBlockSize() == size)
			return _allocators[i];
	}
	
	return NULL;
}

/// Insert an allocator instance into the array
/// @param[in] allocator - An allocator instance
static inline void insert_allocator(Allocator* allocator)
{
	for (INT i=0; i<MAX_ALLOCATORS; i++)
	{
		if (_allocators[i] == 0)
		{
			_allocators[i] = allocator;
			return;
		}
	}
	
	ASSERT();
}

/// This function must be called exactly one time *before* the operating system
/// threading starts. When the system is still single threaded at startup,
/// the xallocator API does not need lock protection.
extern "C" void xalloc_init()
{
	lock_init();
}

/// Called one time when the application exits to cleanup any allocated memory.
extern "C" void xalloc_destroy()
{
#ifndef STATIC_POOLS
	lock_get();

	for (INT i=0; i<MAX_ALLOCATORS; i++)
	{
		if (_allocators[i] == 0)
			break;
		delete _allocators[i];
		_allocators[i] = 0;
	}

	lock_release();
#endif

	lock_destroy();
}

/// Get an Allocator instance based upon the client's requested block size.
/// If a Allocator instance is not currently available to handle the size,
///	then a new Allocator instance is create.
///	@param[in] size - the client's requested block size.
///	@return An Allocator instance that handles blocks of the requested
///	size.
extern "C" Allocator* xallocator_get_allocator(size_t size)
{
	// Based on the size, find the next higher powers of two value.
	// Add sizeof(Allocator*) to the requested block size to hold the size
	// within the block memory region. Most blocks are powers of two,
	// however some common allocator block sizes can be explicitly defined
	// to minimize wasted storage. This offers application specific tuning.
	size_t blockSize;
	if (size > 256 && size <= 396)
		blockSize = 396;
	else if (size > 512 && size <= 768)
		blockSize = 768;
	else
		blockSize = nexthigher<size_t>(size + sizeof(Allocator*));	

	Allocator* allocator = find_allocator(blockSize);

#ifdef STATIC_POOLS
	ASSERT_TRUE(allocator != NULL);
#else
	// If there is not an allocator already created to handle this block size
	if (allocator == NULL)  
	{
		// Create a new allocator to handle blocks of the size required
		allocator = new Allocator(blockSize, 0, 0, "xallocator");

		// Insert allocator into array
		insert_allocator(allocator);
	}
#endif
	
	return allocator;
}

/// Allocates a memory block of the requested size. The blocks are created from
///	the fixed block allocators.
///	@param[in] size - the client requested size of the block.
/// @return	A pointer to the client's memory block.
extern "C" void *xmalloc(size_t size)
{
	lock_get();

	// Allocate a raw memory block 
	Allocator* allocator = xallocator_get_allocator(size);
	void* blockMemoryPtr = allocator->Allocate(size);

	lock_release();

	// Set the block Allocator* within the raw memory block region
	void* clientsMemoryPtr = set_block_allocator(blockMemoryPtr, allocator);
	return clientsMemoryPtr;
}

/// Frees a memory block previously allocated with xalloc. The blocks are returned
///	to the fixed block allocator that originally created it.
///	@param[in] ptr - a pointer to a block created with xalloc.
extern "C" void xfree(void* ptr)
{
	if (ptr == 0)
		return;

	// Extract the original allocator instance from the caller's block pointer
	Allocator* allocator = get_block_allocator(ptr);

	// Convert the client pointer into the original raw block pointer
	void* blockPtr = get_block_ptr(ptr);

	lock_get();

	// Deallocate the block 
	allocator->Deallocate(blockPtr);

	lock_release();
}

/// Reallocates a memory block previously allocated with xalloc.
///	@param[in] ptr - a pointer to a block created with xalloc.
///	@param[in] size - the client requested block size to create.
extern "C" void *xrealloc(void *oldMem, size_t size)
{
	if (oldMem == 0)
		return xmalloc(size);

	if (size == 0) 
	{
		xfree(oldMem);
		return 0;
	}
	else 
	{
		// Create a new memory block
		void* newMem = xmalloc(size);
		if (newMem != 0) 
		{
			// Get the original allocator instance from the old memory block
			Allocator* oldAllocator = get_block_allocator(oldMem);
			size_t oldSize = oldAllocator->GetBlockSize() - sizeof(Allocator*);

			// Copy the bytes from the old memory block into the new (as much as will fit)
			memcpy(newMem, oldMem, (oldSize < size) ? oldSize : size);

			// Free the old memory block
			xfree(oldMem);

			// Return the client pointer to the new memory block
			return newMem;
		}
		return 0;
	}
}

/// Output xallocator usage statistics
extern "C" void xalloc_stats()
{
	lock_get();

	for (INT i=0; i<MAX_ALLOCATORS; i++)
	{
		if (_allocators[i] == 0)
			break;

		if (_allocators[i]->GetName() != NULL)
			cout << _allocators[i]->GetName();
		cout << " Block Size: " << _allocators[i]->GetBlockSize();
		cout << " Block Count: " << _allocators[i]->GetBlockCount();
		cout << " Blocks In Use: " << _allocators[i]->GetBlocksInUse();
		cout << endl;
	}

	lock_release();
}


