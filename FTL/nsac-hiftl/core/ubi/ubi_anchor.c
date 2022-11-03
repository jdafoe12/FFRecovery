/*********************************************************
 * Module name: ubi_anchor.c
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
 *    anchor block.
 *
 *********************************************************/

#include <core\inc\cmn.h>
#include <core\inc\mtd.h>
#include <sys\sys.h>
#include "ubi_inc.h"

/*********************************************************
 * Funcion Name: anchor_find_next_block
 *
 * Description:
 *    get a free and good block in anchor block region.
 *
 * Return Value:
 *    STATUS      S/F
 *
 * Parameter List:
 *    current_block  IN/OUT   the current block, and the
 *                            next free good block as
 *                            return value.
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
static STATUS anchor_find_next_block(PHY_BLOCK* current_block);

ANCHOR_TABLE anchor_table;

/* anchor block tracker, the first blocks are reserved as anchor blocks. */
static PHY_BLOCK anchor_block = 0;
static PAGE_OFF anchor_next_page = 0;

STATUS ANCHOR_Format(PHY_BLOCK total_data_block) {
  LOG_BLOCK b;
  STATUS ret;

  anchor_block = ANCHOR_FIRST_BLOCK;
  anchor_next_page = 0;

  anchor_table.total_data_block = total_data_block;
  anchor_table.previous_anchor_block = INVALID_BLOCK;
  anchor_table.swl_current_area = 0;
  memcpy(anchor_table.oath, ANCHOR_OATH, 11);
  anchor_table.version = ANCHOR_VERSION;

  /* ERASE all anchor blocks */
  for (b = ANCHOR_FIRST_BLOCK; b <= ANCHOR_LAST_BLOCK; b++) {
    if (ANCHOR_IsBadBlock(b) == FALSE) {
      ret = MTD_Erase(b);
      if (ret != STATUS_SUCCESS) {
        /* mark bad block */
        ANCHOR_LogBadBlock(b);
      }
    }
  }
  return ANCHOR_Update();
}

STATUS ANCHOR_Init() {
  PHY_BLOCK block;
  PHY_BLOCK previous_block1 = INVALID_BLOCK;
  PAGE_OFF anchor_page1 = INVALID_PAGE;
  PHY_BLOCK anchor_block1 = INVALID_BLOCK;
  PAGE_OFF anchor_current_page;
  PHY_BLOCK old_block;
  STATUS ret;

  /* set the default anchor table, and all other tables */
  memset(&anchor_table, 0xff, MPP_SIZE);
  memset(&index_table, 0xff, MPP_SIZE);

  /* TODO: validate BBR issue in anchor blocks:
   * may find anchor table in MANY blocks. Choose the updated one according to
   * the plr info of anchor block reclaim.
   */
  for (block = ANCHOR_FIRST_BLOCK; block <= ANCHOR_LAST_BLOCK; block++) {
    /* try to read the 1st page to see if the block is empty or not */
    anchor_current_page = 0;
    ret = TABLE_Read(block, &anchor_current_page, NULL);
    if (ret == STATUS_SUCCESS) {
      /* read anchor table from the last valid page */
      anchor_current_page = INVALID_PAGE;
      ret = TABLE_Read(block, &anchor_current_page, &anchor_table);
    }

    if (ret == STATUS_SUCCESS) {
      if (anchor_block1 == INVALID_BLOCK) {
        anchor_block1 = block;
        anchor_page1 = anchor_current_page;
        previous_block1 = anchor_table.previous_anchor_block;
      } else {
        if (previous_block1 == block) {
          /* the first found block is the updated block */
          old_block = block;
        } else {
          /* this block is the updated anchor block */
          ASSERT(anchor_block1 == anchor_table.previous_anchor_block);
          anchor_block1 = block;
          anchor_page1 = anchor_current_page;
          old_block = anchor_table.previous_anchor_block;
        }
        /* erase the out of date block */
        if (old_block != INVALID_BLOCK) {
          ret = MTD_Erase(old_block);
          if (ret != STATUS_SUCCESS) {
            ANCHOR_LogBadBlock(old_block);
          }
        }
        /* only two blocks may have anchor table, so break now */
        break;
      }
    }
  }
  if (anchor_block1 != INVALID_BLOCK) {
    anchor_block = anchor_block1;
    anchor_next_page = anchor_page1 + 1;
    ret = STATUS_SUCCESS;
  } else {
    ret = STATUS_FAILURE;
  }
  if (ret == STATUS_SUCCESS) {
    if (anchor_table.total_data_block == INVALID_BLOCK
        || anchor_table.version != ANCHOR_VERSION
        || memcmp(anchor_table.oath, ANCHOR_OATH, 11) != 0) {
      /* just a valid bad block table */
      ret = STATUS_FAILURE;
    }
  }
  
  return ret;
}

void ANCHOR_LogBadBlock(PHY_BLOCK block) {
  UINT32 i;

  for (i = 0; i < ANCHOR_BADBLOCK_COUNT; i++) {
    if (anchor_table.bad_block_table[i] == INVALID_BLOCK) {
      anchor_table.bad_block_table[i] = block;
      break;
    }
    if (anchor_table.bad_block_table[i] == block) {
      break;
    }
  }
  return;
}

BOOL ANCHOR_IsBadBlock(PHY_BLOCK block) {
  UINT32 i;
  BOOL ret = FALSE;

  for (i = 0; i < ANCHOR_BADBLOCK_COUNT; i++) {
    if (anchor_table.bad_block_table[i] == block) {
      ret = TRUE;
      break;
    }
  }

  return ret;

}

STATUS ANCHOR_Update() {
  STATUS ret = STATUS_FAILURE;
  BOOL anchor_reclaimed = FALSE;

  while (ret != STATUS_SUCCESS) {
    if (anchor_next_page == PAGE_PER_PHY_BLOCK) {
      /* reclaim anchor block, fill new plr info data */
      anchor_table.previous_anchor_block = anchor_block;
      ret = anchor_find_next_block(&anchor_block);
      if (ret == STATUS_SUCCESS) {
        anchor_next_page = 0;
        anchor_reclaimed = TRUE;
      } else {
        break;
      }
    }
    /* write anchor table */
    ret = TABLE_Write(anchor_block, anchor_next_page, &anchor_table);
    if (ret == STATUS_SUCCESS) {
      if (anchor_reclaimed == TRUE) {
        /* ERASE the pervious anchor table block */
        (void) MTD_Erase(anchor_table.previous_anchor_block);
      }
      anchor_next_page++;
    } else if (ret == STATUS_BADBLOCK) {
      /* handle bad block in anchor */
      anchor_next_page = PAGE_PER_PHY_BLOCK;
    }
  }
  return ret;
}

void ANCHOR_IndexReclaim(PHY_BLOCK index_current_block,
                         ERASE_COUNT index_current_ec, PHY_BLOCK new_itb,
                         ERASE_COUNT new_itb_ec) {
  anchor_table.index_new_block = new_itb;
  anchor_table.index_new_ec = new_itb_ec;
  anchor_table.index_old_block = index_current_block;
  anchor_table.index_old_ec = index_current_ec;
}

static STATUS anchor_find_next_block(PHY_BLOCK* current_block) {
  STATUS ret = STATUS_SUCCESS;
  PHY_BLOCK next_block = *current_block;

  do {
    next_block = next_block + 1;
    if (next_block > ANCHOR_LAST_BLOCK) {
      next_block = ANCHOR_FIRST_BLOCK;
    }

    /* return the next non-bad block in anchor blocks */
    if (ANCHOR_IsBadBlock(next_block) == FALSE) {
      ret = MTD_Erase(next_block);
      if (ret != STATUS_SUCCESS) {
        /* mark bad block */
        ANCHOR_LogBadBlock(next_block);
      }
    } else {
      ret = STATUS_BADBLOCK;
    }
  } while (ret != STATUS_SUCCESS && next_block != *current_block);

  if (next_block != *current_block) {
    *current_block = next_block;
  } else {
    ret = STATUS_FAILURE;
  }

  return ret;
}
  
//jsj 添加开始
void ANCHOR_Format_Clean() {
  PHY_BLOCK b;
  STATUS ret = STATUS_SUCCESS;
  /* ERASE all anchor blocks */
  for (b = ANCHOR_FIRST_BLOCK; b <= ANCHOR_LAST_BLOCK; b++) 
  {
    ret = MTD_Erase(b);
    if (ret != STATUS_SUCCESS) {
 
    }
  }
} 


//jsjpenn
STATUS UBI_ANCHOR_table_store(){
  
  STATUS ret;
  LOG_BLOCK block_to_write=55;//UBI_reserved_START_BLOCK =55;
  PAGE_OFF page_to_write=17;//写在index table之后，即第17页
  
  PHY_BLOCK block;    
  block=AREA_GetBlock(block_to_write);//写入的物理块号
  
  
  ret=UBI_Write(block_to_write, page_to_write, &anchor_table, NULL, FALSE);
  
   if(ret == STATUS_SUCCESS) {
    uart_printf("%s: Write anchor table to %d PEB ok\r\n",__func__,block); //PEB=62   
  }  
  
  return ret;
}

STATUS UBI_ANCHOR_table_restore(){
  STATUS ret;
  
  LOG_BLOCK block_to_write=55;//UBI_reserved_START_BLOCK =55;
  PAGE_OFF page_to_write=17;//写在index table之后，即第17页
  
  //在恢复index table时保证index block PEB没变，最多只是indedx block的擦除次数变化
  //所以在恢复anchor table时，直接恢复保存的anchor table即可
  
  //以下过程只是将保存的anchor table写到anchor block中的6个之一，但还不执行ANCHOR_Update()
  //在更新完所有UBI和FTL表后，
  //再将保存的anchor table读回，再执行ANCHOR_Update()
  
  PHY_BLOCK block;  
  block=AREA_GetBlock(block_to_write); //保存的area table写入的物理块号  
  ANCHOR_TABLE tem_anchor_table;  
  ret=UBI_Read(block_to_write, page_to_write, &tem_anchor_table, NULL);  
  if(ret == STATUS_SUCCESS) {
    uart_printf("%s: Read stored anchor table from %d PEB ok\r\n",__func__,block);    
  }
  
  //将保存的anchor table写入现在的anchor block中
  if (anchor_next_page == PAGE_PER_PHY_BLOCK) {      
      anchor_table.previous_anchor_block = anchor_block;
      ret = anchor_find_next_block(&anchor_block);
      uart_printf("%s: Find another anchor block=%d\r\n",__func__,anchor_block);
      if (ret == STATUS_SUCCESS) {
       (void) MTD_Erase(anchor_table.previous_anchor_block);
        anchor_next_page = 0; 
       ret = TABLE_Write(anchor_block, anchor_next_page, &tem_anchor_table);
       if (ret == STATUS_SUCCESS) {
        uart_printf("%s: Rewrite old anchor table to %d page of %d PEB ok \r\n",__func__,anchor_next_page,anchor_block);   
       }      
      }
 
  }else{
    //write anchor table
    ret = TABLE_Write(anchor_block, anchor_next_page, &tem_anchor_table);
    if (ret == STATUS_SUCCESS) {
      uart_printf("%s: Rewrite old anchor table to %d page of %d PEB ok \r\n",__func__,anchor_next_page,anchor_block);   
    }
  }
   
 return ret;
}
        
STATUS Truly_UBI_ANCHOR_table_restore(){
  STATUS ret= STATUS_SUCCESS;  
  anchor_next_page++;
  return ret;
}