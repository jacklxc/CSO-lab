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

/* Basic constants and macros*/
#define WSIZE 4 //Word and header/footer size (bytes)
#define DSIZE 8 //Double word size (bytes)
#define CHUNKSIZE (1<<12) //Extend heap by this amount (bytes)

#define MAX(x,y) ((x)>(y))? (x):(y))

/* Pack a size and allocated bit into a word */
#define PACK(size,alloc) ((size)|(alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p,val)c (*(unsigned int *)(p)=(val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p)& ~0x7)
#define GET_ALLOC(p) (GET(p)& 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp)-WSIZE)
#define FTRP(bp) ((char*)(bp)+GET_SIZE(HDRP(bp))-DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((*char)(bp)+GET_SIZE(((char *)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((*char)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))

/* Function declarations */
int mm_init(void);
void* mm_malloc(size_t size);
void mm_free(void *ptr);
void* mm_realloc(void *ptr, size_t size);
int mm_check(void);

/* Private global variables */
static char *heap_listp; //Points to the first byte in the heap
static char *mem_brk; //Points to the last byte of heap +1
static char *mem_max_addr; //The max heap address +1

/* 
 * mm_init - initialize the malloc package.
 * Returns 0 if successful and return -1 if not.
 */
int mm_init(void)
{
	/* Create the initial empty heap */
	if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
		return -1;
	PUT(heap_listp,0);//Alignment padding
	PUT(heap_listp + (1*WSIZE),PACK(DSIZE,1)); //Prologue header
	PUT(heap_listp + (2*WSIZE),PACK(DSIZE,1)); //Prologue footer
	PUT(heap_listp + (3*WSIZE),PACK(0,1)); //Epilogue header
	heap_listp += (2*WSIZE);
	
	/* Extend the empty heap with a free block of CHUNKSIZE bytes */
	if (extend_heap(CHUNKSIZE/WSIZE)==NULL)
		return -1;
    return 0;
}

static void *extend_heap(size_t words){
	char *bp;
	size_t size;

	/* Allocate an even number of words to maintain alignment */
	size = (words%2)? (words+1)*WSIZE : words * WSIZE;
	if ((long)(bp = mem_sbrk(size))==-1)
		return NULL;
	
	/* Initialize free block header/footer and the epilogue header */
	PUT(HDRP(bp), PACK(size,0)); //Free block header
	PUT(FTRP(bp), PACK(size,0)); //Free block footer
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1)); //New epilogue header
	
	/* Coalesce if the previous block was free */
	return coalesce(bp);
}



/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
	return NULL;
    else {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
	size_t sizce = GET_SIZE(HDRP(ptr));
	PUT(HDRP(ptr),PACK(size,0));
	PUT(FTRP(ptr),PACK(size,0));
	coalesce(ptr);
}

static void *coalesce(void *bp){
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if(prev_alloc&&next_alloc){
		return bp;
	}
	else if (prev_alloc && !next_alloc){
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	else if (!prev_alloc && next_alloc){
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_blkp(bp);
    }
	else{
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
	return bp;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}


void mm_checkheap(int verbose) 
{
	return;
}










