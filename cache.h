/* This file contains a rough implementation of an L1 cache in the absence of an L2 cache*/
#include <stdlib.h>
#include <stdio.h>

struct cache_blk_t { // note that no actual data will be stored in the cache 
  unsigned long tag;
  char valid;
  char dirty;
  char in_higher_cache; //to be used to see if the block is conccurrently in an L1 cache while looking to evict blocks from L2
  unsigned LRU;	//to be used to build the LRU stack for the blocks in a cache set
};

struct cache_t {
	// The cache is represented by a 2-D array of blocks. 
	// The first dimension of the 2D array is "nsets" which is the number of sets (entries)
	// The second dimension is "assoc", which is the number of blocks in each set.
  int nsets;					// number of sets
  int blocksize;				// block size
  int assoc;					// associativity
  int mem_latency;				// the miss penalty
  char cache_type;     //1 or 2 for L1 or L2
  struct cache_blk_t **blocks;	// a pointer to the array of cache blocks
};

struct cache_t * cache_create(int size, int blocksize, int assoc, int mem_latency)
{
  int i, nblocks , nsets ;
  struct cache_t *C = (struct cache_t *)calloc(1, sizeof(struct cache_t));
		
  nblocks = size *1024 / blocksize ;// number of blocks in the cache
  nsets = nblocks / assoc ;			// number of sets (entries) in the cache
  C->blocksize = blocksize ;
  C->nsets = nsets  ; 
  C->assoc = assoc;
  C->mem_latency = mem_latency;

  C->blocks= (struct cache_blk_t **)calloc(nsets, sizeof(struct cache_blk_t *));

  for(i = 0; i < nsets; i++) {
		C->blocks[i] = (struct cache_blk_t *)calloc(assoc, sizeof(struct cache_blk_t));
	}
  return C;
}
//------------------------------

int updateLRU(struct cache_t *cp ,int index, int way)
{
int k ;
for (k=0 ; k< cp->assoc ; k++) 
{
  if(cp->blocks[index][k].LRU < cp->blocks[index][way].LRU) 
     cp->blocks[index][k].LRU = cp->blocks[index][k].LRU + 1 ;
}
  cp->blocks[index][way].LRU = 0 ;
  return 0;
}

int update_inHigherCache(struct cache_t *cp, unsigned long address, char value)
{
  if (value != 0 && value != 1)
    return 1; //invalid input

  int way, block_address, index, tag ;

  block_address = (address / cp->blocksize);
  tag = block_address / cp->nsets;
  index = block_address - (tag * cp->nsets) ;

  for (way = 0 ; way < cp->assoc ; way++)
  {
    if (cp->blocks[index][way].tag == tag && cp->blocks[index][way].valid == 1)
    {
      cp->blocks[index][way].in_higher_cache = value;
      return 0;
    }
  }

  return 1; //error, block not found
}

int evict_block(struct cache_t *cp, unsigned long address)
{
  int way, block_address, index, tag, latency ;
  latency = 0;

  block_address = (address / cp->blocksize);
  tag = block_address / cp->nsets;
  index = block_address - (tag * cp->nsets) ;

  for (way = 0 ; way < cp->assoc ; way++)
  {
    if (cp->blocks[index][way].tag == tag && cp->blocks[index][way].valid == 1)
    {
      if (cp->blocks[index][way].dirty == 1)/*if has to write back*/
        latency += cp->mem_latency;

      cp->blocks[index][way].valid = 0;
      return latency;
    }
  }
  return latency; //block not found
}

int cache_access(struct cache_t *cp, unsigned long address, int access_type /*0 for read, 1 for write*/, struct cache_t *L2cp, struct cache_t *L1cp)
{
  int i,latency ;
  int block_address ;
  int index;
  int tag;
  int way, backup_way ;
  int max, backup_max ;

  block_address = (address / cp->blocksize);
  tag = block_address / cp->nsets;
  index = block_address - (tag * cp->nsets) ;


  /* a cache hit */
  latency = 0;
  for (i = 0; i < cp->assoc; i++) {	/* look for the requested block */
    if (cp->blocks[index][i].tag == tag && cp->blocks[index][i].valid == 1) {
    updateLRU(cp, index, i) ;
    if (access_type == 1) cp->blocks[index][i].dirty = 1 ;
    return(latency);					
    }
  }


  /* a cache miss */
  for (way=0 ; way< cp->assoc ; way++)		/* look for an invalid entry */
  {    
      if (cp->blocks[index][way].valid == 0) 
      {
    	  latency = latency + cp->mem_latency;	/* account for reading the block from memory*/
    		/* should instead read from L2, in case you have an L2 */
        cp->blocks[index][way].valid = 1 ;
        cp->blocks[index][way].tag = tag ;

        if (cp->cache_type == 2)
        {
          cp->blocks[index][way].in_higher_cache = 1;
        }

    	  updateLRU(cp, index, way); 
    	  cp->blocks[index][way].dirty = 0;
        if(access_type == 1) cp->blocks[index][way].dirty = 1 ;
    	  return(latency);				/* an invalid entry is available*/
      }
  }

  //for when they are all valid, execute below
  max = cp->blocks[index][0].LRU ;	/* find the LRU block */
  way = 0 ;

  backup_max = max;/* These will be used if all blocks are in L1 as well as L2 and we have to evict from both */
  backup_way = way;

  int num_in_higher_cache = 0;
  for (i=1 ; i< cp->assoc ; i++)
  { 
    if (cp->blocks[index][i].LRU > max) 
    {

      if (cp->cache_type == 2) //if we are in L2, check if this block is also in L1
      {

        if (cp->blocks[index][i].in_higher_cache == 0) //if its not also in L1, then allow it to have the chance to be evicted
        {
          max = cp->blocks[index][i].LRU ;
          way = i ;
        }
        else
        {
          num_in_higher_cache++;
        }

        backup_max = cp->blocks[index][i].LRU;
        backup_way = i;

      }
      else if (cp->cache_type == 1)
      {
        max = cp->blocks[index][i].LRU ;
        way = i ;
      }

    }
  }

  /* if all the blocks in this index are also in L1 so we have to evict LRU for L2 in both caches */
  if (num_in_higher_cache == cp->assoc && cp->cache_type == 2)
  {
    latency += evict_block(L1cp, address);
    way = backup_way;
  }

  if (cp->blocks[index][way].dirty == 1)  
  	latency = latency + cp->mem_latency;	/* for writing back the evicted block */
  
  latency = latency + cp->mem_latency;		/* for reading the block from memory*/

  if (cp->cache_type == 1 && L2cp != NULL) //we are in L1 and there is an L2 available
  {
    //since we are in L1 and are about to evict, we need to make sure the same block in L2 does not say its in L1 anymore
    int error = update_inHigherCache(L2cp, address, 0);
    // if (error)
    //   printf("\nERROR: could not find block in L2 cache to update\n");
    // else
    //   printf("\nIT WORKED\n");
  }

  if (cp->cache_type == 2)//we are in L2 and we need to make sure the inclusive bit is 1
    cp->blocks[index][way].in_higher_cache = 1 ;

  cp->blocks[index][way].tag = tag ;
  updateLRU(cp, index, way) ;
  cp->blocks[index][i].dirty = 0 ;
  if(access_type == 1) cp->blocks[index][i].dirty = 1 ;
  return(latency) ;
}
