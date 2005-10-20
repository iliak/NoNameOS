/*
 *     AAA    M M    OOO    SSSS
 *    A   A  M M M  O   O  S 
 *    AAAAA  M M M  O   O   SSS
 *    A   A  M   M  O   O      S
 *    A   A  M   M   OOO   SSSS 
 *
 *    Author:  Stephen Fewer
 *    License: GNU General Public License (GPL)
 */
 
#include <kernel/mm/mm.h>
#include <kernel/mm/physical.h>
#include <kernel/mm/paging.h>
#include <kernel/console.h>
#include <kernel/kernel.h>

void * mm_heapTop = NULL;
void * mm_heapBottom = NULL;

void mm_init( DWORD memUpper )
{	
	// setup the physical memory manager, after this we can use physical_pageAlloc()
	physical_init( memUpper );

	// setup and initilise paging
	paging_init();
	
	// from here on in we can use mm_malloc() & mm_free()
}

// this probably doesnt belong here but it'll do for now :)
BYTE * mm_memset( BYTE * dest, BYTE val, int count )
{
    BYTE * temp = dest;
    for( ; count !=0 ; count-- )
    	*temp++ = val;
    return dest;
}
/*
void mm_memcpy( BYTE * dest, BYTE * src, int count )
{
  DWORD i = 0;
  for( ; i < count ; i++ )
      dest[i] = src[i]; 
}
*/

// increase the heap by some amount, this will be rounded up by the page size 
void * mm_morecore( DWORD size )
{
	// calculate how many pages we will need
	int pages = ( size / PAGE_SIZE ) + 1;
	// when mm_heapTop == NULL we must create the initial heap
	if( mm_heapTop == NULL )
		mm_heapBottom = mm_heapTop = (void *)KERNEL_HEAP_VADDRESS;
	// set the address to return
	void * prevTop = mm_heapTop;
	// create the pages
	for( ; pages-->0 ; mm_heapTop+=PAGE_SIZE )
	{
		// alloc a physical page in mamory
		void * physicalAddress = physical_pageAlloc();
		if( physicalAddress == 0L )
			return NULL;
		// map it onto the end of the heap
		paging_setPageTableEntry( mm_heapTop, physicalAddress, TRUE );
		// clear it for safety
		mm_memset( mm_heapTop, 0x00, PAGE_SIZE );
	}
	// return the start address of the memory we allocated to the heap
	return prevTop;
}

// free a previously allocated item from the heap
void mm_free( void * address )
{
	kernel_lock();
	struct HEAP_ITEM * tmp_item;
	struct HEAP_ITEM * item = (struct HEAP_ITEM *)( address - sizeof(struct HEAP_ITEM) );
	// find it
	for( tmp_item=mm_heapBottom ; tmp_item!=NULL ; tmp_item=tmp_item->next )
	{
		if( tmp_item == item )
			break;
	}
	// not found
	if( tmp_item == NULL )
	{
		kernel_unlock();
		return;
	}
	// free it
	tmp_item->used = FALSE;
	// coalesce any free adjacent items
	for( tmp_item=mm_heapBottom ; tmp_item!=NULL ; tmp_item=tmp_item->next )
	{
		while( !tmp_item->used && tmp_item->next!=NULL && !tmp_item->next->used )
		{
			tmp_item->size += sizeof(struct HEAP_ITEM) + tmp_item->next->size;
			tmp_item->next = tmp_item->next->next;
		}
	}
	kernel_unlock();
}

// allocates an arbiturary size of memory (via first fit) from the kernel heap
void * mm_malloc( DWORD size )
{
	kernel_lock();
	struct HEAP_ITEM * new_item, * tmp_item;
	int total_size;
	// sanity check
	if( size == 0 )
	{
		kernel_unlock();
		return NULL;
	}
	// round up by 8 bytes and add header size
	total_size = ( ( size + 7 ) & ~7 ) + sizeof(struct HEAP_ITEM);
	// search for first fit
	for( new_item=mm_heapBottom ; new_item!=NULL ; new_item=new_item->next )
	{
		if( !new_item->used && (total_size <= new_item->size) )
			break;
	}
	// if we found one
	if( new_item != NULL )
	{
		tmp_item = (struct HEAP_ITEM *)( (int)new_item + total_size );
		tmp_item->size = new_item->size - total_size;
		tmp_item->used = FALSE;
		tmp_item->next = new_item->next;
		
		new_item->size = size;
		new_item->used = TRUE;
		new_item->next = tmp_item;
	}
	else
	{
		// didnt find a fit so we must increase the heap to fit
		new_item = mm_morecore( total_size );
		if( new_item == NULL )
		{
			kernel_unlock();
			return NULL;	
		}
		// create an empty item for the extra space mm_morecore() gave us
		// we can calculate the size because morecore() allocates space that is page aligned
		tmp_item = (struct HEAP_ITEM *)( (int)new_item + total_size );
		tmp_item->size = PAGE_SIZE - (total_size%PAGE_SIZE ? total_size%PAGE_SIZE : total_size) - sizeof(struct HEAP_ITEM);
		tmp_item->used = FALSE;
		tmp_item->next = NULL;
		// create the new item
		new_item->size = size;
		new_item->used = TRUE;
		new_item->next = tmp_item;
	}
	// return the newly allocated memory location
	kernel_unlock();
	return (void *)( (int)new_item + sizeof(struct HEAP_ITEM) );
}
