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
 *
 * Explicit free list
 * Pointers, headers, and footers are 64 bits = 8 bytes = double words
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

/* Size constants (in bytes) */
#define WORD 8
#define DWORD 16
#define MIN_BLOCK_SIZE 32

/* Default size for extending heap */
#define CHUNKSIZE (1 << 12)

/* Returns the larger of two values */
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned long *)(p))
#define PUT(p, val) (*(unsigned long *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x0F)
#define GET_ALLOC(p) (GET(p) & 0x01)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WORD)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DWORD)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WORD)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DWORD)))

/* Private global variables - integers, floats, and pointers only */
static char *heap_start;

/* Interface function prototypes */
int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);
void mm_checkheap(int verbose);

/* Helper function prototypes */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t size);
static void place(void *bp, size_t size);

int mm_init(void) {
    //create initial empty heap
    heap_start = (char *)mem_sbrk(MIN_BLOCK_SIZE);
    if (heap_start == (void *)-1)
        return -1;
    //alignment padding
    PUT(heap_start, 0);
    //prologue header
    PUT(heap_start + 1*WORD, PACK(DWORD, 1));
    //prologue footer
    PUT(heap_start + 2*WORD, PACK(DWORD, 1));
    //epilogue header
    PUT(heap_start + 3*WORD, PACK(0, 1));
    //set heap_start to the prologue
    heap_start += 2*WORD;
    //get free space for the heap
    if (extend_heap(CHUNKSIZE/WORD) == NULL)
        return -1;
    return 0;
}

static void *extend_heap(size_t words) {
    //ensure number of words for the new block is even
    size_t size = (words % 2 == 0) ? words*WORD : (words + 1)*WORD;
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
    if (size <= DWORD)
        adj_size = 2*DWORD;
    else
        adj_size = DWORD*((size + DWORD + DWORD - 1)/DWORD);
    //find sufficiently-sized free block
    void *bp = find_fit(adj_size);
    //if a suitable block is found, place it there
    if (bp != NULL)
        place(bp, adj_size);
    //otherwise, extend the heap and then place
    else {
        size_t extend_size = MAX(adj_size, CHUNKSIZE);
        bp = extend_heap(extend_size/WORD);
        if (bp != NULL)
            place(bp, adj_size);
    }
    return bp;
}

static void *find_fit(size_t size) {
    //iterate over blocks until a free block with enough size is found
    for (void *bp = heap_start; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
        if (!GET_ALLOC(HDRP(bp)) && size <= GET_SIZE(HDRP(bp)))
            return bp;
    //no blocks fit, return NULL
    return NULL;
}

static void place(void *bp, size_t size) {
    size_t block_size = GET_SIZE(HDRP(bp));
    //if there is excess space in the current block, split the block
    if (block_size - size >= 2*DWORD) {
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
    //realloc is equivalent to malloc if ptr is NULL
    if (ptr == NULL)
        return mm_malloc(size);
    //realloc is equivalent to free if size is 0
    if (size <= 0) {
        mm_free(ptr);
        return NULL;
    }
    //ensure size is aligned
    size_t adj_size = MAX(ALIGN(size) + DWORD, MIN_BLOCK_SIZE);
    size_t block_size = GET_SIZE(HDRP(ptr));
    //currently allocated space doesn't need to grow
    if (adj_size <= block_size) {
        //split if difference is larger than the minimum block size
        if (block_size - adj_size > MIN_BLOCK_SIZE) {
            PUT(HDRP(ptr), PACK(adj_size, 1));
            PUT(FTRP(ptr), PACK(adj_size, 1));
            PUT(HDRP(NEXT_BLKP(ptr)), PACK(block_size - adj_size, 1));
            mm_free(NEXT_BLKP(ptr));
        }
        return ptr;
    }
    //more space needed
    else {
        void *new_ptr = mm_malloc(size);
        //malloc fails, so realloc also fails
        if (new_ptr == NULL)
            return NULL;
        //copy old data
        memcpy(new_ptr, ptr, block_size);
        //free ptr
        mm_free(ptr);
        return new_ptr;
    }
}

void mm_checkheap(int verbose) {
    return;
}
