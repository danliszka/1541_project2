/**************************************************************/
/* CS/COE 1541
   just compile with gcc -o CPU CPU.c
   and execute using
   ./CPU  /afs/cs.pitt.edu/courses/1541/short_traces/sample.tr	0
***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include "CPU.h"

// to keep cache statistics
unsigned int I_accesses = 0;
unsigned int I_misses = 0;
unsigned int D_read_accesses = 0;
unsigned int D_read_misses = 0;
unsigned int D_write_accesses = 0; 
unsigned int D_write_misses = 0;
unsigned int L2_accesses = 0;
unsigned int L2_misses = 0;


#include "cache.h"


//Function prototype
int hazardCheck(struct trace_item *a, struct trace_item *b, struct trace_item *c, struct trace_item *d, struct trace_item *e, struct trace_item *f, struct trace_item *g);


int main(int argc, char **argv)
{
  struct trace_item *tr_entry;
  size_t size;
  char *trace_file_name;
  int trace_view_on = 0;
  int prediction_type = 0;

  //Initialize NOP and Squash instuction
  struct trace_item NOP = {ti_NOP, 0, 0, 0, 0, 0};
  struct trace_item SQUASHED = {'s', 0, 0, 0, 0, 0};

  //Create and initialize pipeline stages
  struct trace_item *IF1 = &NOP;
  struct trace_item *IF2 = &NOP;
  struct trace_item *ID = &NOP;
  struct trace_item *EX = &NOP;
  struct trace_item *MEM1 = &NOP;
  struct trace_item *MEM2 = &NOP;
  struct trace_item *WB = &NOP;

  //Initialize instruction pieces
  unsigned char t_type = 0;
  unsigned char t_sReg_a= 0;
  unsigned char t_sReg_b= 0;
  unsigned char t_dReg= 0;
  unsigned int t_PC = 0;
  unsigned int t_Addr = 0;

  unsigned int cycle_number = 0;

  if (argc == 1) {
    fprintf(stdout, "\nUSAGE: tv <trace_file> <switch - any character>\n");
    fprintf(stdout, "\n(switch) to turn on or off individual item view.\n\n");
    exit(0);
  }


  //--------Modifying so when a trace view and/or prediction type is not specified, it automatically goes to zero.
  trace_file_name = argv[1];
  if (argc == 2)
  {
	  trace_view_on = 0;
	  prediction_type = 0;
  }
  else if (argc == 3)
  {
	  prediction_type = atoi(argv[2]);
	  trace_view_on = 0;

    if (prediction_type != 0)
    {
      printf("\nprediction type can only be 0 in this simulation");
      prediction_type = 0;
    }
  }
  else if (argc == 4) //follows order requirement
  {
	  trace_view_on = atoi(argv[3]);
	  prediction_type = atoi(argv[2]);

    if (prediction_type != 0)
    {
      printf("\nprediction type can only be 0 in this simulation");
      prediction_type = 0;
    }
  }
  else
  {
	  printf("Error: invalid amount of arguments");
	  exit(0);
  }
  //--------end argument modification



  //------Opening configuration file 
  FILE *config_fd;
  config_fd = fopen("cache_config.txt", "rb");

  if (!config_fd) {
    fprintf(stdout, "\ntrace file cache_config.txt not opened.\n\n");
    exit(0);
  }

  unsigned int I_size; 
  unsigned int I_assoc;
  unsigned int D_size;
  unsigned int D_assoc;
  unsigned int L2_size;
  unsigned int L2_assoc;
  unsigned int block_size; //in bytes
  unsigned int L2_access_time; //in cycles
  unsigned int memory_access_time; //in cycles

  fscanf(config_fd, "%u", &I_size);
  fscanf(config_fd, "%u", &I_assoc);
  fscanf(config_fd, "%u", &D_size);
  fscanf(config_fd, "%u", &D_assoc);
  fscanf(config_fd, "%u", &L2_size);
  fscanf(config_fd, "%u", &L2_assoc);
  fscanf(config_fd, "%u", &block_size);
  fscanf(config_fd, "%u", &L2_access_time);
  fscanf(config_fd, "%u", &memory_access_time);

  fclose(config_fd);

  //-------end opening configuration file




  //-------configuration of caches

  struct cache_t *L1_I_CACHE;
  struct cache_t *L1_D_CACHE;
  struct cache_t *L2_CACHE;

  L1_I_CACHE->cache_type = 1;
  L1_D_CACHE->cache_type = 1;
  L2_CACHE->cache_type = 2;

  // initialize L1 Instruction cache
  if (I_size > 0 && L2_size > 0)
    L1_I_CACHE = cache_create(I_size, block_size, I_assoc, L2_access_time);
  else if (I_size > 0 && L2_size == 0)
    L1_I_CACHE = cache_create(I_size, block_size, I_assoc, memory_access_time);
  
  // initialize L1 Data cache
  if (D_size > 0 && L2_size > 0)
    L1_D_CACHE = cache_create(D_size, block_size, D_assoc, L2_access_time);
  else if (D_size > 0 && L2_size == 0)
    L1_D_CACHE = cache_create(D_size, block_size, D_assoc, memory_access_time);

  // initialize L2 cache
  if (L2_size > 0)
    L2_CACHE = cache_create(L2_size, block_size, L2_assoc, memory_access_time);

  //-------end configuration




  //--------Opening simulation file and getting started
  fprintf(stdout, "\n ** opening file %s\n", trace_file_name);

  trace_fd = fopen(trace_file_name, "rb");

  if (!trace_fd) {
    fprintf(stdout, "\ntrace file %s not opened.\n\n", trace_file_name);
    exit(0);
  }

  trace_init();
  //----------------------




  int remaining = 6;
  int hazardType = 0;
  int hashIndex = 0;
  int tag = 0;
  int squashCount = 0;
  int nopCount = 0;
  int foundControlHazard = 0;
  int insertSquash = 0;
  int L1_I_penalty = 0;
  int L1_D_penalty = 0;
  int L2_penalty = 0;

  //execute first cycle
  size = trace_get_item(&tr_entry);
  IF1 = tr_entry;
  cycle_number++;


  while(1) {
    foundControlHazard = 0;

    if (!size && remaining == 0)
    {
      /* no more instructions (trace_items) to simulate */
      printf("+ Simulation terminates at cycle : %u\n", cycle_number);
      printf("squashed instructions: %d\n", squashCount);
      printf("nops inserted: %d\n\n", nopCount);

      unsigned int D_tot_accesses = D_read_accesses + D_write_accesses;
      unsigned int D_tot_hits = D_tot_accesses - (D_read_misses - D_write_misses);
      unsigned int D_tot_misses = D_tot_accesses - D_tot_hits;

      printf("L1 Data cache:          [%u] accesses, [%u] hits, [%u] misses, [%u] miss rate\n", D_tot_accesses, D_tot_hits, D_tot_misses, (D_tot_misses/D_tot_accesses));
      printf("L1 Instruction cache:   [%u] accesses, [%u] hits, [%u] misses, [%u] miss rate\n", I_accesses, (I_accesses - I_misses), I_misses, (I_misses/I_accesses));
      printf("L2 cache:               [%u] accesses, [%u] hits, [%u] misses, [%u] miss rate\n", L2_accesses, (L2_accesses - L2_misses), L2_misses, (L2_misses/L2_accesses));
      break;
    }
    else
    {
      if (!size)
      {
        //Allows the pipeline to coninute to execute the rest of the instructions even though there are no more coming in
        remaining--;
      }

      //check for memory in caches----------------------------
      L1_I_penalty = 0;
      L1_D_penalty = 0;
      L2_penalty = 0;

      //Start with Instruction memory
      I_accesses++;
      if (I_size > 0)
      {
        if (L2_size > 0)
          L1_I_penalty = cache_access(L1_I_CACHE, (unsigned long) IF1->PC, 0, L2_CACHE);
        else
          L1_I_penalty = cache_access(L1_I_CACHE, (unsigned long) IF1->PC, 0, NULL);

        if (L1_I_penalty > 0 && L2_size > 0) //there was an L1 miss and we have an L2 cache
        {
          I_misses++;
          L2_penalty = cache_access(L2_CACHE, (unsigned long)IF1->PC, 0, NULL);
          if (L2_penalty > 0) //there was an L2 miss
          {
            L2_misses++;
            //update that we have instruction also in L1
          }
        }
        else if (L1_I_penalty > 0 && L2_size == 0)
        {
          I_misses++;
        }
      }

      cycle_number += (L1_I_penalty + L2_penalty);
      L1_I_penalty = 0;
      L2_penalty = 0;


      //Next Data memory
      if (MEM1->type == ti_LOAD) //if LOAD instruction
      {
        D_read_accesses++;
        if (D_size > 0)
        {
          if (L2_size > 0)//we have L2 cache
            L1_D_penalty = cache_access(L1_D_CACHE, (unsigned long) MEM1->Addr, 0, L2_CACHE);
          else
            L1_D_penalty = cache_access(L1_D_CACHE, (unsigned long) MEM1->Addr, 0, NULL);

          if (L1_D_penalty > 0 && L2_size > 0) //read miss on L1 and we have L2
          {
            D_read_misses++;
            L2_penalty = cache_access(L2_CACHE, (unsigned long) MEM1->Addr, 0, NULL);
            if (L2_penalty > 0) //L2 cache miss
            {
              L2_misses++;
            }
          }
          else if (L1_D_penalty > 0 && L2_size == 0)
          {
            D_read_misses++;
          }
        }
      }

      else if (MEM1->type == ti_STORE) //if STORE instruction
      {
        D_write_accesses++;
        if (D_size > 0)
        {
          if (L2_CACHE > 0)//we have L2 cache
            L1_D_penalty = cache_access(L1_D_CACHE, (unsigned long) MEM1->Addr, 1, L2_CACHE);
          else
            L1_D_penalty = cache_access(L1_D_CACHE, (unsigned long) MEM1->Addr, 1, NULL);

          if (L1_D_penalty > 0 && L2_size > 0)//write miss on L1 and we have L2
          {
            D_write_misses++;
            L2_penalty = cache_access(L2_CACHE, (unsigned long) MEM1->Addr, 1, NULL);
            if (L2_penalty > 0) //L2 cache miss
            {
              L2_misses++;
            }
          }
          else if (L1_D_penalty > 0 && L2_size == 0)
          {
            D_write_misses++;
          }
        }
      }

      cycle_number += (L1_D_penalty + L2_penalty);
      L1_D_penalty = 0;
      L2_penalty = 0;


      //-------End memory checking in caches

      
      //Check for hazards
      if (insertSquash == 0)
      {

        //Check if there is a control hazard
        if (EX->type == ti_BRANCH || EX->type == ti_JTYPE)
        {

          // No prediction
          if (prediction_type == 0 && EX->Addr == ID->PC) //predicted wrong, branch taken 
          {
            //Squash intstructions
            insertSquash = 3;
            foundControlHazard = 1;
          }

          //We are not going to have other prediction types for this project so I deleted the code to handle those
        }

        
        // If a cycle did not already execute to fix a control hazard
        if (!foundControlHazard)
        {

          //Now check for structural or data hazard
          hazardType = hazardCheck(IF1, IF2, ID, EX, MEM1, MEM2, WB);

          switch (hazardType) {
          case 0: //No hazard, normal cycle
            size = trace_get_item(&tr_entry);
            WB = MEM2;
            MEM2 = MEM1;
            MEM1 = EX;
            EX = ID;
            ID = IF2;
            IF2 = IF1;
            IF1 = tr_entry;
            break;


          case 1: //Structural hazard
            WB = MEM2;
            MEM2 = MEM1;
            MEM1 = EX;
            EX = &NOP;
            hazardType = 0;
            nopCount++;
            break;


          case 2: //Data hazard A
            WB = MEM2;
            MEM2 = MEM1;
            MEM1 = &NOP;
            hazardType = 0;
            nopCount++;
            break;


          case 3: //Data hazard B
            WB = MEM2;
            MEM2 = &NOP;
            hazardType = 0;
            nopCount++;
            break;
         
          }
           
        }
      }
      else
      {
        insertSquash--;
        squashCount++;

        WB = MEM2;
        MEM2 = MEM1;
        MEM1 = EX;
        EX = &SQUASHED;
      }  
    }


    //Parse finishing instruction to be printed
    cycle_number++;
    t_type = WB->type;
    t_sReg_a = WB->sReg_a;
    t_sReg_b = WB->sReg_b;
    t_dReg = WB->dReg;
    t_PC = WB->PC;
    t_Addr = WB->Addr;
    

    if (trace_view_on) {/* print the executed instruction if trace_view_on=1 */
      switch(WB->type) {
        case 's':
          printf("[cycle %d]",cycle_number);
          printf(" SQUASHED Instruction\n");
          break;
        case ti_NOP:
          printf("[cycle %d] NOP\n",cycle_number) ;
          break;
        case ti_RTYPE:
          printf("[cycle %d] RTYPE:",cycle_number) ;
          printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(dReg: %d) \n", WB->PC, WB->sReg_a, WB->sReg_b, WB->dReg);
          break;
        case ti_ITYPE:
          printf("[cycle %d] ITYPE:",cycle_number) ;
          printf(" (PC: %x)(sReg_a: %d)(dReg: %d)(addr: %x)\n", WB->PC, WB->sReg_a, WB->dReg, WB->Addr);
          break;
        case ti_LOAD:
          printf("[cycle %d] LOAD:",cycle_number) ;
          printf(" (PC: %x)(sReg_a: %d)(dReg: %d)(addr: %x)\n", WB->PC, WB->sReg_a, WB->dReg, WB->Addr);
          break;
        case ti_STORE:
          printf("[cycle %d] STORE:",cycle_number) ;
          printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(addr: %x)\n", WB->PC, WB->sReg_a, WB->sReg_b, WB->Addr);
          break;
        case ti_BRANCH:
          printf("[cycle %d] BRANCH:",cycle_number) ;
          printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(addr: %x)\n", WB->PC, WB->sReg_a, WB->sReg_b, WB->Addr);
          break;
        case ti_JTYPE:
          printf("[cycle %d] JTYPE:",cycle_number) ;
          printf(" (PC: %x)(addr: %x)\n", WB->PC,WB->Addr);
          break;
        case ti_SPECIAL:
          printf("[cycle %d] SPECIAL:\n",cycle_number) ;
          break;
        case ti_JRTYPE:
          printf("[cycle %d] JRTYPE:",cycle_number) ;
          printf(" (PC: %x) (sReg_a: %d)(addr: %x)\n", WB->PC, WB->dReg, WB->Addr);
          break;
      }
    }
  }

  trace_uninit();

  exit(0);
}




//retuns an integer based on the hazard
//1 - Structural Hazard, stall IF1, IF2, and ID
//2 - Data Hazard a) stall ID and EX, insert NO-OP into MEM1
//3 - Data Hazard b) stall ID and EX, insert NO-OP into MEM2

int hazardCheck(struct trace_item *IF1inst, struct trace_item *IF2inst, struct trace_item *IDinst, struct trace_item *EXinst, struct trace_item *MEM1inst, struct trace_item *MEM2inst, struct trace_item *WBinst ){
  
  // true if a load is followed by a any inst that could use 
  //the register that is being loadedwhere the branch depends on
  if (MEM1inst->type == ti_LOAD ){
    if((EXinst->type == ti_ITYPE  || EXinst->type == ti_JRTYPE) && EXinst->sReg_a == MEM1inst->dReg)
      return 2;
    else if ((EXinst->type == ti_RTYPE  || EXinst->type == ti_STORE || EXinst->type == ti_BRANCH) && ( EXinst->sReg_b == MEM1inst->dReg || EXinst->sReg_a == MEM1inst->dReg))
      return 2;
  }
  
  // true if a load is followed by a any inst that could use 
  //the register that is being loadedwhere the branch depends on
  if (MEM2inst->type == ti_LOAD ){
    if((EXinst->type == ti_ITYPE  || EXinst->type == ti_JRTYPE) && EXinst->sReg_a == MEM2inst->dReg)
      return 3;
    else if ((EXinst->type == ti_RTYPE || EXinst->type == ti_STORE || EXinst->type == ti_BRANCH) && (EXinst->sReg_b == MEM2inst->dReg || EXinst->sReg_a == MEM2inst->dReg))
      return 3;
  }
  
  // true if the WB instruction is a writing (type 1 or 2) and the ID instruction is a I 
  //or R type and the destination register of WB matches either source register of ID
  if (WBinst->type == ti_RTYPE || WBinst->type == ti_ITYPE){
    if((IDinst->type == ti_ITYPE || IDinst->type ==ti_JRTYPE) && IDinst->sReg_a == WBinst->dReg)
      return 1;
    else if ((IDinst->type == ti_RTYPE  || IDinst->type == ti_STORE || IDinst->type== ti_BRANCH)&& (IDinst->sReg_a == WBinst->dReg || IDinst->sReg_b == WBinst->dReg)) 
      return 1;
  }
    
  
  return 0;
}
