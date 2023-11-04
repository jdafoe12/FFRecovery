/*********************************************************
 * Module name: ftl_bdt.c
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
 *    FTL Block Dirty Table.
 *
 *********************************************************/

#include <core\inc\cmn.h>
#include <core\inc\ubi.h>
#include <sys\sys.h>
#include "ftl_inc.h"

static LOG_BLOCK bdt_current_block;
static PAGE_OFF bdt_current_page;
#define BDT_PAGE_COUNT          ((CFG_LOG_BLOCK_COUNT+MPP_SIZE-1)/MPP_SIZE)//2
DIRTY_PAGE_COUNT block_dirty_table[BDT_PAGE_COUNT * MPP_SIZE];//4096
#define BDT_PAGE_ADDR(i)        (&(block_dirty_table[(i)*MPP_SIZE]))

STATUS BDT_Format() {
  STATUS ret;

  bdt_current_block = BDT_BLOCK0;
  bdt_current_page = 0;

  ret = UBI_Erase(bdt_current_block, bdt_current_block);
  if (ret == STATUS_SUCCESS) {
    /* write to UBI */
    ret = BDT_Commit();
  }

  return ret;
}

STATUS BDT_Init() {
  UINT32 i;
  STATUS ret = STATUS_SUCCESS;

  bdt_current_block = PM_NODE_BLOCK(root_table.bdt_current_journal);
  bdt_current_page = PM_NODE_PAGE(root_table.bdt_current_journal);

  /* read out the valid page of table */
  for (i = 0; i < BDT_PAGE_COUNT; i++) {
    ret = UBI_Read(bdt_current_block, bdt_current_page + i, BDT_PAGE_ADDR(i),NULL);
    ASSERT(ret == STATUS_SUCCESS);
  }

  /* scan the first erased page in the block */
  for (i = bdt_current_page + BDT_PAGE_COUNT; i < PAGE_PER_PHY_BLOCK; i +=BDT_PAGE_COUNT) {
    ret = UBI_Read(bdt_current_block, i, NULL, NULL);
    if (ret != STATUS_SUCCESS) {
      /* this page is not programmed */
      bdt_current_page = i;
      ret = STATUS_SUCCESS;
      break;
    }
  }

  if (i == PAGE_PER_PHY_BLOCK) {
    ASSERT(ret == STATUS_SUCCESS);

    /* current block is full */
    bdt_current_page = PAGE_PER_PHY_BLOCK;
  }

  if (ret == STATUS_SUCCESS) {
    /* skip one page for possible PLR issue */
    
    
   
    
    
    
    (void) BDT_Commit();
  }

  return ret;
}

STATUS BDT_Commit() {
  STATUS ret = STATUS_SUCCESS;
  LOG_BLOCK next_block = INVALID_BLOCK;
  UINT32 i;

  if (bdt_current_page == PAGE_PER_PHY_BLOCK) {
    /* write data in another block */
    next_block = bdt_current_block ^ 1;//BDT���������齻��ʹ��

    /* erase the block before write bdt */
    ret = UBI_Erase(next_block, next_block);
    if (ret == STATUS_SUCCESS) {
      bdt_current_page = 0;
      bdt_current_block = next_block;
    }
  }

  /* write BDT in ram to UBI */
  for (i = 0; i < BDT_PAGE_COUNT; i++) {
    if (ret == STATUS_SUCCESS) {
      ret = UBI_Write(bdt_current_block, bdt_current_page + i, BDT_PAGE_ADDR(i),NULL,FALSE);
    }
  }

  if (ret == STATUS_SUCCESS) {
    PM_NODE_SET_BLOCKPAGE(root_table.bdt_current_journal, bdt_current_block,bdt_current_page);
    bdt_current_page += BDT_PAGE_COUNT;
  }

  return ret;
}


//jsjpenn
STATUS FTL_BDT_table_store(){
  
  STATUS ret;
  LOG_BLOCK block_to_write=FTL_reserved_START_BLOCK;//54
  PAGE_OFF page_to_write=1;//��2ҳBDT��д�ڵ�1�͵�2ҳ
  UINT8 i;  
  
  for (i = 0; i < BDT_PAGE_COUNT; i++) {//2  
    ret=UBI_Write(block_to_write, page_to_write+i, BDT_PAGE_ADDR(i), NULL, FALSE);
  }
  
  return ret;
}

STATUS FTL_BDT_table_restore(){
  
  STATUS ret;
  LOG_BLOCK block_to_write=FTL_reserved_START_BLOCK;//54
  PAGE_OFF page_to_write=1;//��2ҳBDT��д�ڵ�1�͵�2ҳ
  UINT8 i;
  
  
  DIRTY_PAGE_COUNT tem_block_dirty_table[BDT_PAGE_COUNT * MPP_SIZE];//4096
  #define tem_BDT_PAGE_ADDR(i)        (&(tem_block_dirty_table[(i)*MPP_SIZE]))
  
  
  LOG_BLOCK next_block = INVALID_BLOCK;  
   
  for (i = 0; i < BDT_PAGE_COUNT; i++) {  
    ret=UBI_Read(block_to_write, page_to_write+i, tem_BDT_PAGE_ADDR(i), NULL);   
  }
  
  //��Ϊ�ڻָ�MAPIʱ�޸���BDT,����Ҫ���±����BDT�е�PMT����
  for(i = PMT_START_BLOCK; i < PMT_START_BLOCK + PMT_BLOCK_COUNT; i++) {
    tem_block_dirty_table[i]=block_dirty_table[i];  
  }
  
  if(ret == STATUS_SUCCESS) {
    if (bdt_current_page == PAGE_PER_PHY_BLOCK) {    
    next_block = bdt_current_block ^ 1;//BDT���������齻��ʹ��    
    ret = UBI_Erase(next_block, next_block);//ʵ����ֱ��������������޸�MAPII
    if (ret == STATUS_SUCCESS) {
      bdt_current_page = 0;
      bdt_current_block = next_block;
    }
  } 
  for (i = 0; i < BDT_PAGE_COUNT; i++) {
    if (ret == STATUS_SUCCESS) {
      ret = UBI_Write(bdt_current_block, bdt_current_page + i, tem_BDT_PAGE_ADDR(i),NULL,FALSE);
    }
   
  }
  
 }    
  return ret;
}

STATUS Truly_FTL_BDT_table_restore(){
  
  STATUS ret= STATUS_SUCCESS;  
   
  if (ret == STATUS_SUCCESS) {
    PM_NODE_SET_BLOCKPAGE(root_table.bdt_current_journal, bdt_current_block,bdt_current_page);
    bdt_current_page += BDT_PAGE_COUNT;
   
  }  
  return ret;
}

