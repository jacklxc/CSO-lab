/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "NYU Shanghai #1",
    /* First member's full name */
    "Kelvin Liu",
    /* First member's email address */
    "kelvin.liu@nyu.edu",
    /* Second member's full name (leave blank if none) */
    "Xiangci Li",
    /* Second member's email address (leave blank if none) */
    "xl1066@nyu.edu"
};

/* Basic constants and macros */
#define WSIZE 4 //Word and header/footer size (bytes)
#define DSIZE 8 //Double word size (bytes)
#define CHUNKSIZE (1<<12) //Extend heap by this amount (bytes)

/* Returns the larger of two values */
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x07)
#define GET_ALLOC(p) (GET(p) & 0x01)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Private global variables - integers, floats, and pointers only */
static void *heap_listp;

/* Explicit function declarations */
int mm_init(void);
static void* extend_heap(size_t words);
static void *coalesce(void *bp);
void mm_free(void *ptr);
void *mm_malloc(size_t size);
static void *find_fit(size_t size);
static void place(void *bp, size_t size);
void *mm_realloc(void *ptr, size_t size);
void mm_checkheap(int verbose);

int mm_init(void) {
    //create initial empty heap
    heap_listp = mem_sbrk(4*WSIZE);
    if (heap_listp == (void *)-1)
        return -1;
    PUT(heap_listp, 0);
    //prologue header
    PUT(heap_listp + 1*WSIZE, PACK(DSIZE, 1));
    //prologue footer
    PUT(heap_listp + 2*WSIZE, PACK(DSIZE, 1));
    //epilogue header
    PUT(heap_listp + 3*WSIZE, PACK(0, 1));
    //set heap_listp to the prologue
    heap_listp += 2*WSIZE;

    //extend this heap
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

static void *extend_heap(size_t words) {
    //ensure number of words for the new block is even
    size_t size = (words % 2 == 0) ? words*WSIZE : (words + 1)*WSIZE;
    //pointer to the new block
    void *bp = mem_sbrk(size);
    if (bp == (void *)-1)
        return NULL;
    //initialize bp header
    PUT(HDRP(bp), PACK(size, 0));
    //initialize bp footer
    PUT(FTRP(bp), PACK(size, 0));
    //create new epilogue header
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    //coalesce if necessary (previous block was a free block)
    return coalesce(bp);
}

static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    //both previous and next blocks are allocated
    if (prev_alloc && next_alloc)
        return bp;
    //previous block is allocated and next block is free
    else if (prev_alloc && !next_alloc) {
        //increment by the size of the next block
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        //fix header and footer
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    //previous block is free and next block is allocated
    else if (!prev_alloc && next_alloc) {
        //increment by the size of the previous block
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        //fix header and footer
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        //set bp to previous block
        bp = PREV_BLKP(bp);
    }
    //both previous and next blocks are free
    else {
        //increment by the sizes of the previous and next blocks
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        //fix header and footer
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        //set bp to previous block
        bp = PREV_BLKP(bp);
    }
    return bp;
}

void mm_free(void *ptr) {
    //get size of block
    size_t size = GET_SIZE(HDRP(ptr));
    //set header and footer to free
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    //coalesce if necessary
    coalesce(ptr);
}

void *mm_malloc(size_t size) {
    //decline bad input
    if (size == 0)
        return NULL;
    //adjust given size
    size_t adj_size;
    if (size <= DSIZE)
        adj_size = 2*DSIZE;
    else
        adj_size = DSIZE*((size + DSIZE + DSIZE - 1)/DSIZE);
    //find sufficiently-sized free block
    void *bp = find_fit(adj_size);
    //if a suitable block is found, place it there
    if (bp != NULL)
        place(bp, adj_size);
    //otherwise, extend the heap and then place
    else {
        size_t extend_size = MAX(adj_size, CHUNKSIZE);
        bp = extend_heap(extend_size/WSIZE);
        if (bp != NULL)
            place(bp, adj_size);
    }
    return bp;
}

static void *find_fit(size_t size) {
    //iterate over blocks until a free block with enough size is found
    for (void *bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
        if (!GET_ALLOC(HDRP(bp)) && size <= GET_SIZE(HDRP(bp)))
            return bp;
    //no blocks fit, return NULL
    return NULL;
}

static void place(void *bp, size_t size) {
    size_t block_size = GET_SIZE(HDRP(bp));
    //if there is excess space in the current block, split the block
    if (block_size - size >= 2*DSIZE) {
        PUT(HDRP(bp), PACK(size, 1));
        PUT(FTRP(bp), PACK(size, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(block_size - size, 0));
        PUT(FTRP(bp), PACK(block_size - size, 0));
    }
    //otherwise just allocate the whole block
    else {
        PUT(HDRP(bp), PACK(block_size, 1));
        PUT(FTRP(bp), PACK(block_size, 1));
    }
}

void *mm_realloc(void *ptr, size_t size) {
    return ptr;
}

void mm_checkheap(int verbose) {
    return;
}
