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
