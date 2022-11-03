/*********************************************************
 * Module name: ubi_area.c
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
 *    Manage area table in ram and nand.
 *
 *********************************************************/

#include <core\inc\cmn.h>
#include <core\inc\mtd.h>
#include <sys\sys.h>
#include "ubi_inc.h"


typedef struct {
  PHY_BLOCK physical_block;
  ERASE_COUNT physical_block_ec;
} AREA_BLOCK; 


//jsjpenn
/* in var area */

PAGE_OFF area_offset_table[AREA_TABLE_SIZE];//128

//jsjpenn

AREA_BLOCK cached_area_table[CFG_PHY_BLOCK_PER_AREA];//CFG_PHY_BLOCK_PER_AREA=256

static AREA cached_area_number;


STATUS AREA_Init(AREA area_index) {
  STATUS ret;

  /* check the type defination */
  ASSERT(sizeof(PHY_BLOCK) == sizeof(ERASE_COUNT));
  ASSERT(sizeof(PHY_BLOCK) == sizeof(PAGE_OFF));

  /* find the offset of the area table */
  cached_area_number = INVALID_AREA;
  area_offset_table[area_index] = INVALID_PAGE;
  ret = AREA_Read(area_index);

  return ret;
}

PHY_BLOCK AREA_Format(PHY_BLOCK block_count, PHY_BLOCK fmt_current_block,
                      AREA area) {
  UINT32 i;
  STATUS ret = STATUS_SUCCESS;

  memset(&cached_area_table[0], 0xff, MPP_SIZE);

  /* check blocks, log bad block in anchor_table. Program AREA table */
  for (i = 0; i < block_count; fmt_current_block++) {
    if (fmt_current_block < CFG_LOG_BLOCK_COUNT) {
      if (ANCHOR_IsBadBlock(fmt_current_block) == FALSE) {
        ret = MTD_Erase(fmt_current_block);
        if (ret != STATUS_SUCCESS) {
          /* mark bad block */
          ANCHOR_LogBadBlock(fmt_current_block);
        }
      } else {
        ret = STATUS_BADBLOCK;
      }
    } else {
      break;
    }

    if (ret == STATUS_SUCCESS) {
      /* good block, trace in zone table */
      cached_area_table[i].physical_block = fmt_current_block;
      cached_area_table[i].physical_block_ec = 0;
      i++;
    }
  }
   //把最后一个area的不足256块的剩余部分置为全ff
  for (i = block_count; i < CFG_PHY_BLOCK_PER_AREA; i++) {
    /* set all remaining invalid */
    cached_area_table[i].physical_block = INVALID_BLOCK;
    cached_area_table[i].physical_block_ec = INVALID_EC;
  }

  /* write area table, with EC sector, check and erase block first */
  if (ret == STATUS_SUCCESS) {
    while (ANCHOR_IsBadBlock(fmt_current_block) == TRUE) {
      fmt_current_block++;
    }
    ret = MTD_Erase(fmt_current_block);
  }

  if (ret == STATUS_SUCCESS) {
    ret = TABLE_Write(fmt_current_block, 0, &cached_area_table);
 
  }

  if (ret == STATUS_SUCCESS) {
    /* log the area table in index */
    index_table.area_index_table[area] = fmt_current_block;
    index_table.area_index_ect[area] = 0;
    /* log the area offset table */
    area_offset_table[area] = 0;
    fmt_current_block++;
  } else {
    /* mark bad block, pick another block for area table. */
    ANCHOR_LogBadBlock(fmt_current_block);
    fmt_current_block = INVALID_BLOCK;
  }
  return fmt_current_block;
}

BOOL AREA_IsFull(PHY_BLOCK logical_block) {
  STATUS status;
  BOOL ret = FALSE;
  AREA area = AREA_INDEX(logical_block);

  status = AREA_Read(area);
  if (status == STATUS_SUCCESS) {
    /* offset is the current offset, so, -1 */
    //判断该area的area block页是否已用完
    if (area_offset_table[area] == PAGE_PER_PHY_BLOCK - 1) {
      ret = TRUE;
    }
  }
  return ret;
}

BLOCK_OFF AREA_FindMinECBlock(AREA area, PHY_BLOCK* physical_block,
                              ERASE_COUNT* block_ec) {
  STATUS status;
  BLOCK_OFF i;
  BLOCK_OFF min_block_offset = 0;

  status = AREA_Read(area);
  if (status == STATUS_SUCCESS) {
    /* find the min ec block */
    for (i = 0; i < CFG_PHY_BLOCK_PER_AREA; i++) {
      if (cached_area_table[i].physical_block_ec
          < cached_area_table[min_block_offset].physical_block_ec) {
        min_block_offset = i;
      }
    }

    *physical_block = cached_area_table[min_block_offset].physical_block;
    *block_ec = cached_area_table[min_block_offset].physical_block_ec;
  }

  return min_block_offset;
}

BOOL AREA_CheckUpdatePLR(PHY_BLOCK logical_block, PHY_BLOCK origin_block,
                         ERASE_COUNT block_ec) {
  BOOL need_plr = FALSE;
  BLOCK_OFF block_offset = BLOCK_OFFSET_AREA(logical_block);

  /* the area has already been cached in ram table */
  if (cached_area_table[block_offset].physical_block == origin_block
      && cached_area_table[block_offset].physical_block_ec == block_ec) {
    /* no need to update the area table */
    ;
  } else {
    /* the area table is not updated correctly for PL, continue updating */
    cached_area_table[block_offset].physical_block = origin_block;
    cached_area_table[block_offset].physical_block_ec = block_ec;
    need_plr = TRUE;
  }
  return need_plr;
}

STATUS AREA_Update(PHY_BLOCK logical_block, PHY_BLOCK new_origin_block,
                   ERASE_COUNT new_origin_ec) {
  AREA area;
  BLOCK_OFF block;
  PAGE_OFF page = INVALID_PAGE;
  STATUS ret;

  block = BLOCK_OFFSET_AREA(logical_block);
  area = AREA_INDEX(logical_block);

  ret = AREA_Read(area);
  if (ret == STATUS_SUCCESS) {
    if (new_origin_block != INVALID_BLOCK) {
      cached_area_table[block].physical_block = new_origin_block;
      cached_area_table[block].physical_block_ec = new_origin_ec;
    }

    /* set the next area page offset */
    page = (area_offset_table[area] + 1) % PAGE_PER_PHY_BLOCK;
    ret = TABLE_Write(index_table.area_index_table[area], page,
                      &cached_area_table);
  }

  if (ret == STATUS_SUCCESS) {
    ASSERT(page != INVALID_PAGE);
    area_offset_table[area] = page;
  } else if (ret == STATUS_BADBLOCK) {
    /* bad block, set the offset to toggle reclaim of the area table block */
    area_offset_table[area] = PAGE_PER_PHY_BLOCK - 1;
    index_table.area_index_ect[area] = INVALID_EC;
  }
  return ret;
}

/*
OUT: physical block index
IN: logic block index
*/
PHY_BLOCK AREA_GetBlock(LOG_BLOCK logical_block) {
  AREA area;
  STATUS status;
  PHY_BLOCK block;
  PHY_BLOCK ret = INVALID_BLOCK;

  block = BLOCK_OFFSET_AREA(logical_block); // block identifes where is the entry of the mapping from the corresponding area table -- block % block per area
  area = AREA_INDEX(logical_block); // area identifies where is the corresponding area table -- block / block per area

  //uart_printf("Log block: %d\nAreanum: %d\nBlocknum in area: %d\n\n\n", logical_block, area, block);
  
  // JD
  if(ftl_read_state == 0) { // read area from old mapping
  
    status = AREA_Read(area);
    if (status == STATUS_SUCCESS) {
      ret = cached_area_table[block].physical_block;
    }
  }
  else if(ftl_read_state == 1) {
    LOG_BLOCK area_block = 55;
    
    ftl_read_state = 0;
    ret = UBI_Read(area_block, area, &(cached_area_table[0]), NULL);
    ftl_read_state = 1;
    
    ret = cached_area_table[block].physical_block;
    return ret;
    
  }
  // end JD

  return ret;
}

ERASE_COUNT AREA_GetEC(PHY_BLOCK logical_block) {
  AREA area;
  STATUS status;
  PHY_BLOCK block;
  ERASE_COUNT ret = INVALID_EC;

  block = BLOCK_OFFSET_AREA(logical_block);
  area = AREA_INDEX(logical_block);

  status = AREA_Read(area);
  if (status == STATUS_SUCCESS) {
    ret = cached_area_table[block].physical_block_ec;
  }

  return ret;
}

STATUS AREA_Read(AREA area) {
  STATUS ret = STATUS_SUCCESS;

  if (area != cached_area_number) {
    ASSERT(area != INVALID_AREA);
    ret = TABLE_Read(index_table.area_index_table[area],
                     &(area_offset_table[area]), &(cached_area_table[0]));
  }
  if (ret == STATUS_SUCCESS) {
    cached_area_number = area;
  } else {
    cached_area_number = INVALID_AREA;
  }
  return ret;
}


//jsjpenn
STATUS UBI_MAPII_store() {
  STATUS ret;
  UINT8  i,areanum=AREA_COUNT;//总共16个area 
  LOG_BLOCK block_to_write=55;//LEB。UBI_reserved_START_BLOCK =55
  PAGE_OFF page_to_write;
  
  PHY_BLOCK block;   
  block=AREA_GetBlock(block_to_write);//area table写入的物理块号
  
  for(i=0;i<areanum;i++){    
  
    page_to_write=i;    
    
    ret = AREA_Read(i);//将对应的area table读到cached_area_table[]
    if(ret == STATUS_SUCCESS) {        
        ret=UBI_Write(block_to_write, page_to_write, &(cached_area_table[0]), NULL, FALSE);
        uart_printf("%s: write %d area table to %d page of PEB %d ok\r\n",__func__,i,i,block);        
      }    
  }
  
  if(ret == STATUS_SUCCESS) {
    uart_printf("%s: Write all area table to %d PEB ok\r\n",__func__,block); //PEB block=62   
  }
  
  return ret;
}


PHY_BLOCK ValidPEB[4096];//所有保存状态1的image有效数据的物理块号
UINT32  g_ValidPEBNum;


STATUS UBI_Valid_PEB( LOG_BLOCK *ValidLEBTable,UINT32 ValidLEBNum){
  
  STATUS ret=STATUS_SUCCESS;
  UINT32 i,j;
  LOG_BLOCK LEBtemp;
  
  g_ValidPEBNum=ValidLEBNum;
  
  for(i=0;i<g_ValidPEBNum;i++){    
    LEBtemp=ValidLEBTable[i];    
    ValidPEB[i] = AREA_GetBlock(LEBtemp);//获得LEB所对应的PEB    
  }    

  //对ValidPEB进行冒泡排序，从小到大
  for(i=0;i<g_ValidPEBNum;i++){    
    for(j=0;j<g_ValidPEBNum-i-1;j++){               
      if( ValidPEB[j]> ValidPEB[j+1]){
          LEBtemp= ValidPEB[j];
          ValidPEB[j]= ValidPEB[j+1];
          ValidPEB[j+1]=LEBtemp;        
      }    
    }   
  }  
  return ret;
}

extern STATUS index_update();


STATUS UBI_MAPII_restore() { 
  
  STATUS ret;    
  UINT8 i;
  LOG_BLOCK block_stored_UBI=55;//UBI_reserved_START_BLOCK =55;
  PAGE_OFF page_stored_MAPII;//写在0-15页
  
  //现在的area table信息
  PHY_BLOCK now_area_block;
  
  //保存的area table信息
  PHY_BLOCK stored_area_block;
 
  AREA_BLOCK tem_cached_area_table[CFG_PHY_BLOCK_PER_AREA];//256
       
  
  
  //先将保存的index table读到临时变量中
  PHY_BLOCK storePEB;   
  storePEB=AREA_GetBlock(block_stored_UBI);//写入的物理块号
  PAGE_OFF page_stored_INDEX=16;//写在16个area table之后，即第16页  
  INDEX_TABLE tem_index_table;  //保存index table的临时变量 
  ret=UBI_Read(block_stored_UBI, page_stored_INDEX, &tem_index_table, NULL);  
  if(ret == STATUS_SUCCESS) {
     uart_printf("%s: Read stored index table from %d PEB ok\r\n",__func__,storePEB);        
  } 
  
 
  
  for(i=0;i<AREA_COUNT;i++){//总共16个area    
    //读保存的MAPII到临时tem area table中 
    page_stored_MAPII=i;
    //以下读过程是基于现在的area table，而不是保存的area table    
    ret =UBI_Read(block_stored_UBI, page_stored_MAPII, &(tem_cached_area_table[0]), NULL);//读LEB
    
    if(ret == STATUS_SUCCESS) {
      stored_area_block=tem_index_table.area_index_table[i];//原保存的area table所在的PEB
    
      uart_printf("%s: Read the stored %d area block from %d page of PEB %d ok\r\n",__func__,i,i,storePEB);
    }else{
      uart_printf("%s: Read the stored %d area block failed\r\n",__func__,i);
    }
    
     now_area_block=index_table.area_index_table[i];//现在的area table所在的PEB
    //确保area block没有变
     if(stored_area_block==now_area_block){
       uart_printf("%s: Area %d block is the same PEB=%d\r\n",__func__,i,stored_area_block);
       ret=STATUS_SUCCESS;
     }else{       
       //通过修改INDEX_Update_Commit()函数保证各个area block的PEB不变，循环使用
       uart_printf("%s: ERROR: Area %d block has changed, need to do more\r\n",__func__,i);       
     }
    
    
    //判断现在的aera table所在块中的页是否用完,保证还可以继续写入保存的area table    
    if(area_offset_table[i]== PAGE_PER_PHY_BLOCK - 1){//若用完
      uart_printf("%s: Area table block %d is used up\r\n",__func__,i);
      //将现在的area table先读出来        
      ret =TABLE_Read(now_area_block, &(area_offset_table[i]), &(cached_area_table[0]));
      if(ret == STATUS_SUCCESS) {
        cached_area_number=i;//全局变量，代表cached_area_table[]中存的area table号
        uart_printf("%s: Read the now %d area block ok\r\n",__func__,i);
        ret=MTD_Erase(now_area_block);//再将area block PEB物理擦除
        index_table.area_index_ect[i]++;
        if (ret == STATUS_SUCCESS) {//再将现在的area table写回去        
          ret=TABLE_Write(now_area_block, 0, &(cached_area_table[0])); 
          //更新所在页号
         if(ret == STATUS_SUCCESS) {        
           area_offset_table[i]= 0;      
           uart_printf("%s: Rewrite %d new area table ok\r\n",__func__,i);        
         }  
       }else{//若出现坏块，需增加其他逻辑
          uart_printf("%s:Warning-Bad area block in %d need to do more\r\n",__func__,i);
          ANCHOR_LogBadBlock(now_area_block);
        }    
    }     
  }
    
  //将保存的area table也写到area block中，但是先不改变area_offset_table[i]的值
  //注意：写回各个UBI table不能用UBI_Write，要用table_write(),写到具体的物理块上 
  if(ret == STATUS_SUCCESS) {
    ret=TABLE_Write(now_area_block, area_offset_table[i]+1, &(tem_cached_area_table[0]));
    if(ret == STATUS_SUCCESS) {
     uart_printf("%s: Rewrite %d old area table to the %d page of PEB %d ok\r\n",__func__,i,area_offset_table[i]+1,now_area_block);
    }  
   }
  
 }
  
 
  //注意,以上过程只是将保存的area table又写回了area block，但是还使用的是现在的aera table
  //在更新完所有UBI和FTL表后，再更新area_offset_table[i]++
 
 return ret;   
}


      
STATUS Truly_UBI_MAPII_restore() { 
  
  STATUS ret= STATUS_SUCCESS;
   
  UINT8 i; 
  for(i=0;i<AREA_COUNT;i++){
   
   area_offset_table[i]++;
   
  }
  uart_printf("%s:Truly restore old area table ok\r\n",__func__);
 
  return ret;   
}
    
