/*
 * mm.c - a dynamic memory allocator based on a single free list
 *
 * This package replaces the functionality of libc's malloc, free, and realloc
 * functions with `mm_malloc`, `m_free`, and `mm_realloc` respectivly. In order
 * to do so, programs must first initialize the program heap by first calling
 * `mm_init`. This dynamic memory allocator features a single free list ordered
 * by a LIFO method. Free blocks are allocated with a first-fit method, and are
 * coalesced immediately. Similar to libc's malloc, allocated blocks are algined
 * to 16 bytes.
 *
 * The anatomy of a each block:
 *
 * |-----------------------|
 * |         HEADER        |
 * |-----------------------|
 * | PREVIOUS FREE POINTER |\
 * |-----------------------| \
 * |   NEXT FREE POINTER   |  |
 * |-----------------------|  | PAYLOAD (FOR ALLOCATED BLOCKS)
 * |                       |  |
 * |          ...          | /
 * |                       |/
 * |-----------------------|
 * |         FOOTER        |
 * |-----------------------|
 *
 * Each block, whether it is allocated or not, has a header and an identical
 * footer. For allocated blocks, this is not necessary, however it chosen as a
 * simple method to check whether or not a block is truly a block -- if a block
 * has a footer identical to its header, then it is _probably_ a true block. For
 * free blocks, both the header and footer are explicitly necessary in order to
 * do boundary tag coalescing. Both the header and footer is 8 bytes.
 *
 * Free blocks also store two pointers in the payload area of the block. The
 * first points the previous free block in the free list, and the second points
 * to the next free block in the free list. Each of these pointers is also 8
 * bytes, so the minimum block size possible for a normal block is 32 bytes.
 *
 * There are two special blocks -- the prologue and epilogue blocks. As their
 * respective names suggest, the prologue block is the first block, and the
 * epilogue block is the last block. Both of these blocks are marked as
 * allocated. The prologue block serves as a sentinel for the end of the free
 * list because it is the only allocated block in the free list. The epilogue
 * block is not entirely necessary, but it ensures the structural integrity of
 * the heap itself.
 *
 * Please see the function declaration comments for specific details on what
 * each function returns and/or does.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* CONSTANTS AND MACROS */

//size constants (in bytes)
#define WORD 8
#define DWORD 16
#define MIN_BLOCK_SIZE 32
#define CHUNKSIZE (1 << 12)

//returns the larger of two values
#define MAX(x, y) ((x) > (y) ? (x) : (y))

//packs size and allocation bit
#define PACK(size, alloc) ((size) | (alloc))

//access word at address p
#define GET(p) (*(unsigned long *)(p))
#define SET(p, val) (*(unsigned long *)(p) = (val))

//get size and allocated bit from address p
#define GET_SIZE(p) (GET(p) & ~0x0F)
#define GET_ALLOC(p) (GET(p) & 0x01)

//get bp's header and footer
#define HDRP(bp) ((char *)(bp) - WORD)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DWORD)

//get blocks adjacent to bp in memory
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DWORD)))
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))

//get bp's free and next blocks in flist (only if bp is free)
#define PREV_FREE(bp) (*(void **)(bp))
#define NEXT_FREE(bp) (*(void **)((char *)(bp) + WORD))

/* FUNCTION PROTOTYPES */

//helper functions
static void *find_fit(size_t size);
static void *extend_heap(size_t size);
static void allocate(void *bp, size_t size);
static void *coalesce(void *bp);

//linked list functions
static void flist_remove(void *bp);
static void flist_add(void *bp);

//checkheap functions
//TODO: add prototypes for functions related to checkheap

/* GLOBAL VARIABLES */

//team information
team_t team = {
    /* Team name */
    "NYU Shanghai #1",
    /* First member's full name */
    "Kelvin Liu",
    /* First member's email address */
    "kelvin.liu@nyu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};
//pointer to the prologue block on the heap
static void *heap_prologue = NULL;
//pointer to the head of the free list (flist)
static void *flist_head = NULL;

/* MALLOC FUNCTIONS*/

/*
 * mm_init - initializes the malloc package.
 *
 * Returns 0 if the initial heap is successfully created, otherwise, -1. Creates
 * a minimally-sized heap with prologue and epilogue blocks. Does not
 * extend the heap any more than it needs to. The heap looks something like this
 * after initialization:
 *
 *            /-------------- prologue -------------\
 * --------------------------------------------------------------
 * | padding | header | prev_ptr | next_ptr | footer | epilogue |
 * --------------------------------------------------------------
 * ||                |||                   |||                 ||
 *
 * In the diagram above, the space between single bars (|) is 8 bytes wide. By
 * extension, double or triple bars (|| or |||) mark the alignment of the heap.
 * The global variables heap_prologue and flist_head are set to the prologue
 * block. In the case of flist_head, the prologue block is used as a sentinel,
 * as it is the only allocated block allowed in the free list.
 */
int mm_init(void) {
    //get initial space for the heap
    char *heap_start = (char *)mem_sbrk(3*DWORD);
    //error check
    if (heap_start == (char *)-1)
        return -1;
    //set heap_prologue and flist_head
    heap_prologue = (void *)(heap_start + DWORD);
    flist_head = heap_prologue;
    //beginning padding
    SET(heap_start, 0);
    //prologue header and footer
    SET(HDRP(heap_prologue), PACK(MIN_BLOCK_SIZE, 1));
    SET(FTRP(heap_prologue), PACK(MIN_BLOCK_SIZE, 1));
    //prologue pointers
    PREV_FREE(flist_head) = NULL;
    NEXT_FREE(flist_head) = NULL;
    //epilogue
    SET(HDRP(NEXT_BLKP(heap_prologue)), PACK(0, 1));
    return 0;
}

/*
 * mm_malloc - allocate a block of at least size on the heap.
 *
 * Returns a pointer to the allocated block if allocation is successful,
 * otherwise, NULL. Starts by modifying the original size parameter. This is
 * subject to the following:
 * (1) space for the header and footer must be added to the size;
 * (2) the size must be aligned;
 * (3) and the size must be at least the minimum block size.
 * Once the adjusted size is calculated, the free list is searched for a
 * sufficiently-large free block. If no blocks are found, the heap is extended
 * in order to obtain a free block. Once a free block is found, the block is
 * allocated and finally returned.
 */
void *mm_malloc(size_t size) {
    //error check
    if (size <= 0)
        return NULL;
    //calculate the adjusted size
    size_t adj_size = MAX(MIN_BLOCK_SIZE, ALIGN(size + DWORD));
    void *bp = find_fit(adj_size);
    //extend the heap if no free block was found
    if (bp == NULL) {
        //add a free block to the free list
        bp = extend_heap(MAX(CHUNKSIZE, adj_size));
        //report failure if the block pointer is still NULL
        if (bp == NULL)
            return NULL;
    }
    //allocate bp with size adj_size and return
    allocate(bp, adj_size);
    return bp;
}

/*
 * mm_free - free a previously allocated block.
 *
 * The given ptr must be a previously-allocated block, otherwise, this will
 * simply return without having done anything. This is validated by checking
 * whether or not the block reports as allocated. Additionally, the block's
 * header must match the block's footer. This is based on the heuristic that if
 * they do match, the block was probably allocated. This function has undefined
 * behavior for random pointers that pass the test. This function changes
 * ptr's header to reflect that it is free, then calls the coalesce function in
 * order to merge adjacent free blocks, if applicable.
 */
void mm_free(void *ptr) {
    //ensure ptr is valid
    if (!GET_ALLOC(HDRP(ptr)) || GET(HDRP(ptr)) != GET(FTRP(ptr)))
        return;
    //set as free and coalesce
    SET(HDRP(ptr), PACK(GET_SIZE(HDRP(ptr)), 0));
    SET(FTRP(ptr), PACK(GET_SIZE(HDRP(ptr)), 0));
    coalesce(ptr);
}

/*
 * mm_realloc - adjust the size of a previously allocated block.
 *
 * Returns a pointer to the reallocated block if reallocation is successful,
 * otherwise, NULL. If the given ptr is already NULL, the call is equivalent to
 * mm_malloc(size). If the given size is 0, the call is equivalent to
 * mm_free(ptr) and will return NULL. The given ptr must be a previously-
 * allocated block, otherwise this function will fail and return NULL. Similarly
 * to mm_malloc, this function calculates an adjusted size. It then compares
 * the adjusted size to the actual size of the block. If the block size can fit
 * the adjusted size, the pointer is simply returned. If not, this function
 * determines if the block adjacently after the current block is free and has
 * enough space for the adjusted size. If so, the free block is removed from the
 * free list and then the original pointer is returned. Otherwise, mm_malloc is
 * used to get a new pointer to a block sufficiently large and memory is copied
 * from the original block to the new one. Finally, the new pointer is returned.
 */
void *mm_realloc(void *ptr, size_t size) {
    //special cases
    if (ptr == NULL)
        return mm_malloc(size);
    if (GET(HDRP(ptr)) != GET(FTRP(ptr)))
        return NULL;
    if (size <= 0) {
        free(ptr);
        return NULL;
    }
    size_t adj_size = MAX(MIN_BLOCK_SIZE, ALIGN(size + DWORD));
    size_t block_size = GET_SIZE(HDRP(ptr));
    //current block is big enough to fit the given size
    if (adj_size <= block_size) {
        return ptr;
    }
    //adj_size > block_size, so check if next block is free
    void *next_blk = NEXT_BLKP(ptr);
    if (!GET_ALLOC(HDRP(next_blk)) &&
        adj_size <= block_size + GET_SIZE(HDRP(next_blk))) {
        //use logic similar to allocate, remove next_blk from the free list
        flist_remove(next_blk);
        //increase block size
        block_size += GET_SIZE(HDRP(next_blk));
        //check if there's enough room for a free block
        if (block_size - adj_size >= MIN_BLOCK_SIZE) {
            //set the size of the current block's header and footer
            SET(HDRP(ptr), PACK(adj_size, 1));
            SET(FTRP(ptr), PACK(adj_size, 1));
            //set the remaining space as a free block
            SET(HDRP(NEXT_BLKP(ptr)), PACK(block_size - adj_size, 0));
            SET(FTRP(NEXT_BLKP(ptr)), PACK(block_size - adj_size, 0));
            coalesce(NEXT_BLKP(ptr));
        }
        //simply allocate
        else {
            SET(HDRP(ptr), PACK(block_size, 1));
            SET(FTRP(ptr), PACK(block_size, 1));
        }
    }
    //completely new block must be used
    else {
        //save the original ptr
        void *old_ptr = ptr;
        //get new block that is sufficiently big
        ptr = mm_malloc(size);
        //if malloc fails realloc also fails
        if (ptr == NULL)
            return NULL;
        //copy the old data over and free the original block
        memcpy(ptr, old_ptr, block_size - DWORD);
        mm_free(old_ptr);
    }
    //finally return the ptr
    return ptr;
}

/*
 * mm_checkheap - checks the heap's structural integrity and prints info
 */
void mm_checkheap(int verbose) {
    return;
}

/* HELPER FUNCTIONS */

/*
 * find_fit - find a freeblock large enough to fit size
 *
 * Returns the pointer to a free block large enough to fit the given size,
 * otherwise NULL. This function iterates over the free list and examine's each
 * free block's size. It returns the first block found that is large enough to
 * fit size. If the function finishes looking over the list without returning,
 * then there are no blocks large enough, so NULL is returned.
 */
static void *find_fit(size_t size) {
    //iterate over free list
    for (void *bp = flist_head; !GET_ALLOC(HDRP(bp)); bp = NEXT_FREE(bp))
        if (GET_SIZE(HDRP(bp)) >= size)
            return bp;
    return NULL;
}

/*
 * extend_heap - extend the heap by size bytes
 *
 * Returns a pointer to the start of the newly-added heap space if the heap is
 * successfully extended, otherwise, NULL. Uses mem_sbrk to extend the heap. The
 * new heap space is treated as a single free block. Recreates the epilogue
 * block to ensure the integrity to the heap's structure.
 */
static void *extend_heap(size_t size) {
    //get more heap space!
    void *bp = mem_sbrk(size);
    //error check
    if (bp == (void *)-1)
        return NULL;
    //set as a free block
    SET(HDRP(bp), PACK(size, 0));
    SET(FTRP(bp), PACK(size, 0));
    //recreate epilogue
    SET(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    //coalesce if necessary (possible that adjacently previous block is free)
    return coalesce(bp);
}

/*
 * allocate - sets bp as allocated with the given size
 *
 * Compares the size reported by bp's header to the given size. If the
 * difference is large enough for another block, a free block with the size of
 * the difference is created.
 */
static void allocate(void *bp, size_t size) {
    //ensure bp is actually free
    assert(!GET_ALLOC(HDRP(bp)));
    //get the block's actual size
    size_t block_size = GET_SIZE(HDRP(bp));
    //remove the block from the free list
    flist_remove(bp);
    //extra space for a block
    if (block_size - size >= MIN_BLOCK_SIZE) {
        //set the size of the current block's header and footer
        SET(HDRP(bp), PACK(size, 1));
        SET(FTRP(bp), PACK(size, 1));
        //set the remaining space as a free block
        SET(HDRP(NEXT_BLKP(bp)), PACK(block_size - size, 0));
        SET(FTRP(NEXT_BLKP(bp)), PACK(block_size - size, 0));
        coalesce(NEXT_BLKP(bp));
    }
    //simply allocate the block
    else {
        SET(HDRP(bp), PACK(block_size, 1));
        SET(FTRP(bp), PACK(block_size, 1));
    }
}

/*
 * coalesce - checks bp's adjacent neighbors and coalesces adjacent free blocks
 *
 * Returns a pointer the block after coalescing. This function is called every
 * time a free block is created. So naturally, after the block has been
 * coalesced, it also adds the block to the free list as well. There are four
 * possible types of coalescing possible:
 * (1) the next block is free and the previous block isn't;
 * (2) the previous block is free and the next block isn't;
 * (3) both the previous and next blocks are free;
 * (4) and neither the previous nor next blocks are free.
 * In the above cases, `previous` and `next` refer to adjacently previous and
 * adjacently next.
 */
static void *coalesce(void *bp) {
    //ensure block to be coalesced is actually a free block
    assert(!GET_ALLOC(HDRP(bp)));
    //get allocation status of neighbors
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    //case 1
    if (prev_alloc && !next_alloc) {
        //increment size by the additional free space
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        //remove the next block from the free list
        flist_remove(NEXT_BLKP(bp));
    }
    //case 2
    else if (!prev_alloc && next_alloc) {
        //increment size the by additional free space
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        //remove the previous block from the free list
        flist_remove(PREV_BLKP(bp));
        //the previous block becomes the new block
        bp = PREV_BLKP(bp);
    }
    //case 3
    else if (!prev_alloc && !next_alloc) {
        //increment size the by additional free space
        size += (GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp))));
        //remove the previous ane next blocks
        flist_remove(PREV_BLKP(bp));
        flist_remove(NEXT_BLKP(bp));
        //the previous block becomes the new block
        bp = PREV_BLKP(bp);
    }
    //case 4
    else {}
    //adjust the size to reflect the size of the new block
    SET(HDRP(bp), PACK(size, 0));
    SET(FTRP(bp), PACK(size, 0));
    //add coalesced block into the free list and return
    flist_add(bp);
    return bp;
}

/* LINKED LIST FUNCTIONS */

/*
 * flist_remove - removes bp from the free list
 *
 * Just a typical function to remove a node from a doubly-linked list.
 */
static void flist_remove(void *bp) {
    //ensure bp is actually free
    assert(!GET_ALLOC(HDRP(bp)));
    //possible that bp is the head of the list
    if (bp == flist_head)
        flist_head = NEXT_FREE(bp);
    else
        NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
    PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
}

/*
 * flist_add - adds bp to the head of the free list
 *
 * Just a typical function to add a node to the head of a doubly-linked list.
 */
static void flist_add(void *bp) {
    //ensure bp is actually free
    assert(!GET_ALLOC(HDRP(bp)));
    //set the bp's next pointer to the head of the free list
    NEXT_FREE(bp) = flist_head;
    //set the head of the free list's previous pointer to bp
    PREV_FREE(flist_head) = bp;
    //set bp to the head of the free list
    flist_head = bp;
}

/* CHECKHEAP FUNCTIONS */
