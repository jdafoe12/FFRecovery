/*********************************************************
 * Module name: ubi_index.c
 *
 * Copyright 2010, 2011. All Rights Reserved, Crane Chu.
 *
 * This file is part of OpenNFM.
 *
 * OpenNFM is free software: you can redistribute it and/or 
 * modify it under the terms of the GNU General Public 
 * License as published by the Free Software Foundation, 
 * either version 3 of the License, or (at your option) any 
 * later version.
 * 
 * OpenNFM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied 
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE. See the GNU General Public License for more 
 * details.
 *
 * You should have received a copy of the GNU General Public 
 * License along with OpenNFM. If not, see 
 * <http://www.gnu.org/licenses/>.
 *
 * First written on 2010-01-01 by cranechu@gmail.com
 *
 * Module Description:
 *    Manage index table in ram and nand.
 *
 *********************************************************/

#include <core\inc\cmn.h>
#include <core\inc\mtd.h>
#include <sys\sys.h>
#include "ubi_inc.h"

/*
 * ANCHOR table is not managed by FTL/TABLE module. It is self-PLR-safe.
 *
 * To achieve the Static Wear Leveling (SWL), the EC of every PHY_BLOCK
 * is traced in Erase Count Table (ECT), which is located in the following
 * sector of above tables. SWL works as a background task. A block in a area,
 * which has the minimal EC, is selected, and switched with the first
 * block in FBT, only when the difference of their ECs is larger than a
 * predefined threhold. In this way, the static blocks can be found and used.
 *
 * An index table has 4 sectors. The 1st sector is ZIT, the 2nd is ECT
 * of ZIT; the 3rd one has FBT, IBT, CBT, RBT, RFT, and the 4th one is the ECT.
 */

   
//jsjpenn  

STATUS index_update();//为了能在ubi_area.c中调用index_update()



INDEX_TABLE index_table;

/* current index table block, and its erase count */
static PHY_BLOCK index_block;
static ERASE_COUNT index_block_ec;
static PAGE_OFF index_next_page;
static BOOL is_updating_area = FALSE;

PHY_BLOCK INDEX_Format(PHY_BLOCK total_block, PHY_BLOCK fmt_current_block) {
  UINT32 i;
  UINT32 free_block_index = 0;
  PHY_BLOCK index_block = INVALID_BLOCK;
  STATUS ret = STATUS_SUCCESS;

  /* clear plr info */
  index_table.area_update_plr.area_updating_logical_block = INVALID_BLOCK;
  index_table.area_update_plr.area_updating_physical_block = INVALID_BLOCK;
  index_table.area_update_plr.area_updating_block_ec = INVALID_EC;

  /* free block table: all remaining block */
  for (i = 0; fmt_current_block < total_block; fmt_current_block++, i++) {
    /* log good block, discard bad block */
    if (ANCHOR_IsBadBlock(fmt_current_block) == FALSE) {
      ret = MTD_Erase(fmt_current_block);
      if (ret != STATUS_SUCCESS) {
        /* mark bad block */
        ANCHOR_LogBadBlock(fmt_current_block);
      }
    } 
    else {
      ret = STATUS_BADBLOCK;
    }
    if (ret == STATUS_SUCCESS) {
      if (index_block == INVALID_BLOCK) {
        /* the first free block should be reserved for index block */
        index_block = fmt_current_block;
        
      } else {
        /* the reserved free block in block index table should be large
         * enough to hold the 2% reserved free block of total block count.
         */
        ASSERT(free_block_index < FREE_BLOCK_COUNT);
        index_table.free_block_table[free_block_index] = fmt_current_block;
        index_table.free_block_ect[free_block_index] = 0;
        free_block_index++;
      }
    }
  }  
 

  /* fill all remaining free block table as invalid block */
  for (i = free_block_index; i < FREE_BLOCK_COUNT; i++) {
    index_table.free_block_table[i] = INVALID_BLOCK;
    index_table.free_block_ect[i] = INVALID_EC;
  }
  ASSERT(sizeof(index_table) == MPP_SIZE);

  /* write index table, with EC sectors */
  if (index_block == INVALID_BLOCK) {
    ASSERT(ret != STATUS_SUCCESS);
  }

  /* write area table, with EC sector, check and erase block first */
  if (ret == STATUS_SUCCESS) {
    if (ANCHOR_IsBadBlock(index_block) == TRUE) {
      ret = STATUS_BADBLOCK;
    }
  }

  if (ret == STATUS_SUCCESS) {
    ret = MTD_Erase(index_block);
  }

  if (ret == STATUS_SUCCESS) {       
    ret = TABLE_Write(index_block, 0, &index_table);    
  }

  if (ret == STATUS_SUCCESS) {
    /* setup index block info in cfg table */
    anchor_table.index_new_block = index_block;
    anchor_table.index_new_ec = 0;
    anchor_table.index_old_block = INVALID_BLOCK;
    anchor_table.index_old_ec = INVALID_EC;
  } else {
    /* mark bad block, pick another block for index. */
    ANCHOR_LogBadBlock(index_block);
    fmt_current_block = INVALID_BLOCK;
  }
  return fmt_current_block;
}


//jsjpenn
extern PAGE_OFF area_offset_table[AREA_TABLE_SIZE];//128


STATUS INDEX_Init(PHY_BLOCK* logical_block, PHY_BLOCK* origin_block,
                  ERASE_COUNT* block_ec) {
  PAGE_OFF page_offset = INVALID_OFFSET;
  STATUS ret = STATUS_FAILURE;

  is_updating_area = FALSE;

  /* PLR of index block reclaim: try to read new block first */
  ASSERT(anchor_table.index_new_block != INVALID_BLOCK);
  ret = TABLE_Read(anchor_table.index_new_block, &page_offset, &index_table);
  if (ret == STATUS_SUCCESS) {
    /* the new block has valid index table */
    index_block = anchor_table.index_new_block;
    index_block_ec = anchor_table.index_new_ec;
    index_next_page = page_offset + 1;
    /* this page may be written before PL, just write it to overwrite it */
    (void) index_update();
  } else {
    /* can not get correct data from new block, read in old block for PLR */
    ASSERT(anchor_table.index_old_block != INVALID_BLOCK);
    ret = TABLE_Read(anchor_table.index_old_block, &page_offset, &index_table);
    if (ret == STATUS_SUCCESS) {
      /* finish the reclaim before PL */
      index_block = anchor_table.index_old_block;
      index_block_ec = anchor_table.index_old_ec;
      index_next_page = PAGE_PER_PHY_BLOCK;
      /* update index table to new block */
      ret = index_update();
    }
  }

  /* set up the area plr info */
  *logical_block = index_table.area_update_plr.area_updating_logical_block;
  *origin_block = index_table.area_update_plr.area_updating_physical_block;
  *block_ec = index_table.area_update_plr.area_updating_block_ec;
  
  return ret;
}

void INDEX_Update_AreaReclaim(AREA area, PHY_BLOCK new_block, ERASE_COUNT nec) {
  /* release old area block */
  INDEX_FreeBlock_Put(index_table.area_index_table[area],
                      index_table.area_index_ect[area]);

  /* setup new area block */
  index_table.area_index_table[area] = new_block;
  index_table.area_index_ect[area] = nec;
}

void INDEX_Update_AreaUpdate(LOG_BLOCK logical_block, PHY_BLOCK physical_block,
                             ERASE_COUNT block_ec) {
  ASSERT(physical_block != INVALID_BLOCK && block_ec != INVALID_EC);

  index_table.area_update_plr.area_updating_logical_block = logical_block;
  index_table.area_update_plr.area_updating_physical_block = physical_block;
  index_table.area_update_plr.area_updating_block_ec = block_ec;
  is_updating_area = TRUE;
}

/* update index table, and area table if necessary */
STATUS INDEX_Update_Commit() {
  AREA area;
  BOOL area_reclaim = FALSE;
  STATUS ret = STATUS_SUCCESS;

  PHY_BLOCK updating_logical_block;
  PHY_BLOCK updating_physical_block;
  
  ERASE_COUNT updating_block_ec;

  
  do {
    if (is_updating_area == TRUE) {
      updating_logical_block = index_table.area_update_plr.area_updating_logical_block;
      updating_physical_block = index_table.area_update_plr.area_updating_physical_block;
      updating_block_ec = index_table.area_update_plr.area_updating_block_ec;

      /* update area table, the 2nd-level index table */
      if (AREA_IsFull(updating_logical_block) == TRUE) {//判断某个LEB所在area的area block中的页是否用完
        /* update area table in another new area table block.
         * the new block is only valid after writing index table, so no PLR
         * issue between writing these two blocks.*/
        
        //jsjpenn        
        //原来的逻辑是：如果某个area的area block中的页用完时，从FBT中得到一个PEB，然后修改Index table中
        //中的idnex_table.area_index_table[]
        //为了保证各个area block的PEB不变，从而方便恢复，改为直接对原PEB进行物理擦除，且不修改index table        
        area = AREA_INDEX(updating_logical_block);
        ret=MTD_Erase(index_table.area_index_table[area]);//直接对原PEB进行物理擦除        
     
        if (ret == STATUS_SUCCESS) {
          /* update info of area index table */
          //原来的逻辑：将原area table的PEB放回到FBT中
          //更新index_table.area_index_table[]和index_table.area_index_ect[]
          //INDEX_Update_AreaReclaim(area, new_area_block, new_area_ec);
          area_reclaim = TRUE;
        }else{
          uart_printf("***%s***: Error: erase %d area block PEB fail\r\n",__func__,area);          
        }
        
       //jsjpennend   
        
      } else {
        /* update index table with area update info for PLR.
         * PLR: get the area update info from index table, and check the
         *      area table if need to update due to PL.*/
        ret = index_update();
      }
      if (ret == STATUS_SUCCESS) {
        /* write area block */
        //更新MAPII映射，并将更新后的映射写入对应的area block PEB中
        ret = AREA_Update(updating_logical_block, updating_physical_block, updating_block_ec);
        if (ret == STATUS_SUCCESS && area_reclaim == TRUE) {
          /* update index later than area, if area block reclaimed */
          ret = index_update();
        }
      }
      if (ret == STATUS_SUCCESS) {
        /* CLEAR the area update flag */
        is_updating_area = FALSE;
      }
    } else {
      /* only update index table */
      ret = index_update();
    }
  } while (ret == STATUS_BADBLOCK);
  return ret;
}

void INDEX_FreeBlock_GetMaxECBlock(PHY_BLOCK* physical_block,
                                   ERASE_COUNT* block_ec) {
  UINT32 i;
  
  
  //jsjpenn
  UINT32 j;
  UINT32 judge=0;
  
  for (i = FREE_BLOCK_COUNT - 1; i > 0; i--) {
    if (index_table.free_block_ect[i] != INVALID_EC) {
      *physical_block = index_table.free_block_table[i];
      *block_ec = index_table.free_block_ect[i];
      
    }
  }

  return;
}

void INDEX_FreeBlock_SwapMaxECBlock(PHY_BLOCK min_ec_block, ERASE_COUNT min_ec) {
  UINT32 i;

  /* swap for SWL:
   * - find the max ec good block in free block,
   * - re-sort and inset the min ec block
   */

  /* GET the max ec good block in the sorted free block */
  for (i = FREE_BLOCK_COUNT - 1; i > 0; i--) {
    if (index_table.free_block_ect[i] != INVALID_EC) {
      break;
    }
  }

  if (index_table.free_block_ect[i] != INVALID_EC
      && index_table.free_block_ect[i] > min_ec
      && index_table.free_block_ect[i] - min_ec > STATIC_WL_THRESHOLD) {
    /* insert the min ec block to sorted free block table,
     * continue to scan the free block table to head.
     */
    for (i = i - 1; i > 0; i--) {
      if (index_table.free_block_ect[i] > min_ec) {
        index_table.free_block_table[i + 1] = index_table.free_block_table[i];
        index_table.free_block_ect[i + 1] = index_table.free_block_ect[i];
      } else {
        /* insert the min ec block in current position */
        index_table.free_block_table[i + 1] = min_ec_block;
        index_table.free_block_ect[i + 1] = min_ec;

        break;
      }
    }
    /* an special case due to i is unsigned char */
    if (i == 0) {
      if (index_table.free_block_ect[0] > min_ec) {
        index_table.free_block_table[1] = index_table.free_block_table[0];
        index_table.free_block_ect[1] = index_table.free_block_ect[0];

        index_table.free_block_table[0] = min_ec_block;
        index_table.free_block_ect[0] = min_ec;
      } else {
        /* insert the min ec block in current position */
        index_table.free_block_table[1] = min_ec_block;
        index_table.free_block_ect[1] = min_ec;
      }
    }
  }
}

/* the EC of the free block in FBT:
 * MSB: set when read/program fail, reset if erased successfully
 * when erasing a block in FBT failed, just discard it, and get a new block
 * from IBT! If none in IBT, discard the bad, and get another in FBT.
 */
STATUS INDEX_FreeBlock_Get(DIE_INDEX die, PHY_BLOCK* block, ERASE_COUNT* ec) {
  STATUS ret;
  UINT32 i;

  die = die % TOTAL_DIE_COUNT;//0
  
  uart_printf("**%s**:called\r\n", __func__);
  
  do {
    for (i = 0; i < FREE_BLOCK_COUNT; i++) {//125
      /* get new block at the head of free block table,
       * whose ec is min, and in the same die. */
      if (index_table.free_block_table[i] == INVALID_BLOCK) {
        i = FREE_BLOCK_COUNT;
        break;
      } else if ((index_table.free_block_table[i] % TOTAL_DIE_COUNT) == die) {
        *block = index_table.free_block_table[i];
        break;
      }
    }
    //for循环遍历所有free block后，遇到第一个无效块后，返回i=FREE_BLOCK_COUNT
    if (i == FREE_BLOCK_COUNT) {
      /* can not find the block in the same die, get another */
      i = 0;
      *block = index_table.free_block_table[0];
    }
    //返回同一个die中的FBT的第一项，即EC最小的free block 
    if (*block != INVALID_BLOCK) {
      ASSERT(i < FREE_BLOCK_COUNT);

      /* no background erase. Erase block before using it. Most of erase
       * would happen in background reclaim.
       */
      ret = MTD_Erase(*block);//真正的擦除从FBT中返回的EC最小的物理块 
      //jsjpenn
      if(ret==STATUS_MTDEraseReturnFail){
        uart_printf("*%s*: MTD Erase PEB %d Return Fail\r\n", __func__,*block);        
      }else{
        uart_printf("*%s*: MTD Erase PEB %d Return ok\r\n", __func__,*block);
      }
      
    } else {
      ret = STATUS_TOOMANY_BADBLOCK;
    }

    if (ret != STATUS_SUCCESS && ret!=STATUS_MTDEraseReturnFail) {
      /* discard and log the bad block */
      ANCHOR_LogBadBlock(*block);
    }

    if (ret == STATUS_SUCCESS) {
      /* increase erase count of the new free block */
      *ec = index_table.free_block_ect[i] + 1;
    }
    //将FBT各项前移    
    if (ret != STATUS_TOOMANY_BADBLOCK) {
      /* move forward all other blocks, discard current block in FBT */

      for (; i < FREE_BLOCK_COUNT - 1; i++) {
        index_table.free_block_table[i] = index_table.free_block_table[i + 1];
        index_table.free_block_ect[i] = index_table.free_block_ect[i + 1];
      }
      /* fill the last free block entry with invalid block info */
      ASSERT(i == FREE_BLOCK_COUNT - 1);
      index_table.free_block_table[i] = INVALID_BLOCK;
      index_table.free_block_ect[i] = INVALID_EC;
    }
    /* if not get a free block, and there is good block left (e.g. not
     * too many bad block generated.), try again. */
  } while (ret != STATUS_SUCCESS && ret != STATUS_TOOMANY_BADBLOCK
      && ret != STATUS_SimulatedPowerLoss);//ret只要等于三个当中其中某一个值就停止循环

  return ret;
}

void INDEX_FreeBlock_Put(PHY_BLOCK dirty_block, ERASE_COUNT dirty_block_ec) {
  UINT32 i;
  
   //将物理块加入FBT (按EC递增排序) 
   //由于从free block往外分配时会先检查是否是坏块，所以不必再此检查是否为坏块  
  /* the last item of FBT will be discarded to insert the new free block */
  for (i = FREE_BLOCK_COUNT - 2; i > 0; i--) {
    /* search the max ec block less than dirty_block_ec */
    if (index_table.free_block_ect[i] > dirty_block_ec) {
      index_table.free_block_table[i + 1] = index_table.free_block_table[i];
      index_table.free_block_ect[i + 1] = index_table.free_block_ect[i];
    } else {
      break;
    }
  }
  /* insert new free block at the position of i+1, or at the beginning */
  if (i > 0) {
    index_table.free_block_table[i + 1] = dirty_block;
    index_table.free_block_ect[i + 1] = dirty_block_ec;
  } else {
    ASSERT(i == 0);
    if (index_table.free_block_ect[0] > dirty_block_ec) {
      index_table.free_block_table[1] = index_table.free_block_table[0];
      index_table.free_block_ect[1] = index_table.free_block_ect[0];
      index_table.free_block_table[0] = dirty_block;
      index_table.free_block_ect[0] = dirty_block_ec;
    } else {
      index_table.free_block_table[1] = dirty_block;
      index_table.free_block_ect[1] = dirty_block_ec;
    }
  }  
}


//jsjpenn  

STATUS index_update() {//为了能在ubi_area.c中调用index_update()
  //ERASE_COUNT ec;
  //PHY_BLOCK free_block;
  STATUS ret = STATUS_SUCCESS;

  /* Reclaim and PLR:
   *
   * If area block is updated successfully, its data is valid, and
   * mismatch with index table. So, a PLR info is required in index table.
   *
   * Process:
   * - find the new block, and log PLR info to cfg table.
   *   PLR: find the PLR info in cfg table, try to read
   *        index table in new block. If failed, read the table
   *        still from the old block. Omit the latest reclaim
   *        log, and start another reclaim. The new block is still
   *        a free block in FBT, neither used, nor lost. Only need
   *        to do another erase before get it, and the EC is not
   *        updated, but it is not a big issue due to few PL happened.
   *        We just guartee the integrity and functionality of system.
   * - write index table to new block, with the updated free table
   *   and its ECT.
   *   PLR: nothing to do. The new block is integrity now.
   */
  if (index_next_page == PAGE_PER_PHY_BLOCK) {
    /* log reclaim in cfg table */
    
    //jsjpenn        
    //原来的逻辑是：如果index block中的页用完时，从FBT中得到一个PEB，然后修改anchor table中对应的index table相应变量
    //为了保证各个index block的PEB不变，从而方便恢复，改为直接对原PEB进行物理擦除，且不修改anchor table  
           
    //ret = INDEX_FreeBlock_Get(index_block, &free_block, &ec);
     ret=MTD_Erase(index_block);
  

    if (ret == STATUS_SUCCESS) {
     
      index_next_page = 0;
      
      index_block_ec++;
    }else{
      uart_printf("***%s***: Error: erase index block PEB fail\r\n",__func__);
    }    
   //jsjpennend 
    
  }

  if (ret == STATUS_SUCCESS) {
    /* one page write in index block, NO PLR issue */
    ret = TABLE_Write(index_block, index_next_page, &index_table);
  }

  if (ret == STATUS_SUCCESS) {
    index_next_page++;
  } else if (ret == STATUS_BADBLOCK) {
    /* bad block, set the next page offset to toggle index block reclaim. */
    index_next_page = PAGE_PER_PHY_BLOCK;
    /* discard the bad block */
    index_block_ec = INVALID_EC;
  }
  return ret;
}

//jsjpenn
STATUS UBI_INDEX_table_store(){
  
  STATUS ret;
  LOG_BLOCK block_to_write=55;//UBI_reserved_START_BLOCK =55;
  PAGE_OFF page_to_write=16;//写在16个area table之后，即第16页
  
  PHY_BLOCK block;    
  block=AREA_GetBlock(block_to_write);//index table写入的物理块号
  
  ret=UBI_Write(block_to_write, page_to_write, &index_table, NULL, FALSE);
  
   //if(ret == STATUS_SUCCESS) {
    //uart_printf("%s: Write index table to %d PEB ok\r\n",__func__,block); //PEB=62   
  //}
  
  return ret;
}


UINT8 g_indextable_sign;//全局变量
//保存的index table信息        
PHY_BLOCK g_old_index_block;//全局变量
UINT32 g_old_index_block_ec;//全局变量

STATUS UBI_INDEX_table_restore(){
  STATUS ret;
  LOG_BLOCK block_stored_UBI=55;//UBI_reserved_START_BLOCK =55; 
  PAGE_OFF page_stored_INDEX=16;//保存的index table
  PAGE_OFF page_stored_ANCHOR=17;//anchor table写在index table之后，即第17页
  
  //保存的index table信息        
  PHY_BLOCK old_index_block;
  //现有的index table信息
  PHY_BLOCK new_index_block; 
  
  PHY_BLOCK storePEB;   
  storePEB=AREA_GetBlock(block_stored_UBI);//UBI table写入的物理块号
  
  //先将保存的index table读到临时变量中 
  INDEX_TABLE tem_index_table;  
  ret =UBI_Read(block_stored_UBI, page_stored_INDEX, &tem_index_table, NULL);//读LEB
  if(ret == STATUS_SUCCESS){     
    uart_printf("%s: Read stored index table from %d PEB ok\r\n",__func__,storePEB);    
  }
  
  //先将保存的anchor table读到临时变量中  
  ANCHOR_TABLE tem_anchor_table;
  ret=UBI_Read(block_stored_UBI, page_stored_ANCHOR, &tem_anchor_table, NULL);
  if(ret == STATUS_SUCCESS){     
    uart_printf("%s: Read stored anchor table from %d PEB ok\r\n",__func__,storePEB);    
  }
    
  //保证index table block PEB不变 
  old_index_block=tem_anchor_table.index_new_block; //PEB 
  new_index_block=anchor_table.index_new_block;//PEB
  
  uart_printf("%s: old_index_block=%d\r\n",__func__,old_index_block);
  uart_printf("%s: new_index_block=%d\r\n",__func__,new_index_block);
  uart_printf("%s: index_block=%d\r\n",__func__,index_block);//全局变量
  
  if(old_index_block==new_index_block && old_index_block==index_block){//全局变量index_block
   g_indextable_sign=0; //表示index blcok没变
   uart_printf("%s: Index block is the same\r\n",__func__);
   if (index_next_page == PAGE_PER_PHY_BLOCK) {//若index block中页用完
     ret=MTD_Erase(old_index_block);//直接物理擦除PEB
     //(tem_anchor_table.index_new_ec)++;
     if (ret == STATUS_SUCCESS) {    
       ret=TABLE_Write(old_index_block, 0, &index_table);//再将现在index table写回去 
       if(ret == STATUS_SUCCESS) {        
         index_next_page=1;//全局变量     
         uart_printf("%s: Rewrite new index table ok\r\n",__func__);        
       } 
      index_block_ec++;//全局变量     
     }else if (ret != STATUS_SUCCESS) {//todo:若出现坏块，需增加其他逻辑        
      ANCHOR_LogBadBlock(old_index_block);
      uart_printf("**%s**:ERROR-Bad index block need to do more\r\n",__func__);
     }
   }
   //再将保存的index table写回去    
   if(ret == STATUS_SUCCESS) {   
     ret=TABLE_Write(index_block, index_next_page, &tem_index_table);    
     if(ret == STATUS_SUCCESS) {
      uart_printf("%s: Rewrite old index table to the %d page of old index block PEB %d ok\r\n",__func__,index_next_page,old_index_block);
    }  
  }
   
 }else{
   //已在index_update()中保证index block PEB不变，循环使用    
   g_indextable_sign=1;//标记位，表示index blcok改变
   uart_printf("%s: Index block has changed\r\n",__func__); 
   g_old_index_block=old_index_block;
   ret=MTD_Erase(old_index_block);//直接物理擦除PEB
   //(tem_anchor_table.index_new_ec)++;//不用加了
   if (ret == STATUS_SUCCESS) {
    g_old_index_block_ec= tem_anchor_table.index_new_ec;
    ret=TABLE_Write(old_index_block, 0, &tem_index_table);//直接将保存的index table写到原index block中
    if(ret == STATUS_SUCCESS) {            
      uart_printf("%s: Rewrite old index table to old index block ok\r\n",__func__);       
    } 
         
   }else if (ret != STATUS_SUCCESS) {//若出现坏块，需增加其他逻辑        
     ANCHOR_LogBadBlock(old_index_block);
     uart_printf("**%s**:ERROR-Bad index block need to do more\r\n",__func__);
   }
 
  }
 
 //在恢复area table时保证各area block PEB没变，最多只是area block的擦除次数变化
 //所以在恢复index table时，直接恢复保存的index table即可
 
  //以上过程只是将保存的index table写到原index block中，但还不执行index_update()
  //在更新完所有UBI和FTL表后，需要先判断标记位，再从不同的位置将保存的index table读回，再执行index_update()

  return ret;
}


STATUS Truly_UBI_INDEX_table_restore(){
  STATUS ret= STATUS_SUCCESS;
  
  if(g_indextable_sign==1){//表示index blcok改变
    
    index_block = g_old_index_block;
    index_next_page = 1;
    index_block_ec = g_old_index_block_ec; 
    
    uart_printf("%s: Restore index table ok-changed index block\r\n",__func__); 
  }
  
  if(g_indextable_sign==0){//表示index blcok不变
    
    index_next_page++; 
    uart_printf("%s: Restore index table ok-unchanged index block\r\n",__func__);
  }  
  
  return ret;
}