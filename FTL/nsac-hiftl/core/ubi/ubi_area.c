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
   //�����һ��area�Ĳ���256���ʣ�ಿ����Ϊȫff
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
    //�жϸ�area��area blockҳ�Ƿ�������
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
  UINT8  i,areanum=AREA_COUNT;//�ܹ�16��area 
  LOG_BLOCK block_to_write=55;//LEB��UBI_reserved_START_BLOCK =55
  PAGE_OFF page_to_write;
  
  PHY_BLOCK block;   
  block=AREA_GetBlock(block_to_write);//area tableд���������
  
  for(i=0;i<areanum;i++){    
  
    page_to_write=i;    
    
    ret = AREA_Read(i);//����Ӧ��area table����cached_area_table[]
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


PHY_BLOCK ValidPEB[4096];//���б���״̬1��image��Ч���ݵ�������
UINT32  g_ValidPEBNum;


STATUS UBI_Valid_PEB( LOG_BLOCK *ValidLEBTable,UINT32 ValidLEBNum){
  
  STATUS ret=STATUS_SUCCESS;
  UINT32 i,j;
  LOG_BLOCK LEBtemp;
  
  g_ValidPEBNum=ValidLEBNum;
  
  for(i=0;i<g_ValidPEBNum;i++){    
    LEBtemp=ValidLEBTable[i];    
    ValidPEB[i] = AREA_GetBlock(LEBtemp);//���LEB����Ӧ��PEB    
  }    

  //��ValidPEB����ð�����򣬴�С����
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
  PAGE_OFF page_stored_MAPII;//д��0-15ҳ
  
  //���ڵ�area table��Ϣ
  PHY_BLOCK now_area_block;
  
  //�����area table��Ϣ
  PHY_BLOCK stored_area_block;
 
  AREA_BLOCK tem_cached_area_table[CFG_PHY_BLOCK_PER_AREA];//256
       
  
  
  //�Ƚ������index table������ʱ������
  PHY_BLOCK storePEB;   
  storePEB=AREA_GetBlock(block_stored_UBI);//д���������
  PAGE_OFF page_stored_INDEX=16;//д��16��area table֮�󣬼���16ҳ  
  INDEX_TABLE tem_index_table;  //����index table����ʱ���� 
  ret=UBI_Read(block_stored_UBI, page_stored_INDEX, &tem_index_table, NULL);  
  if(ret == STATUS_SUCCESS) {
     uart_printf("%s: Read stored index table from %d PEB ok\r\n",__func__,storePEB);        
  } 
  
 
  
  for(i=0;i<AREA_COUNT;i++){//�ܹ�16��area    
    //�������MAPII����ʱtem area table�� 
    page_stored_MAPII=i;
    //���¶������ǻ������ڵ�area table�������Ǳ����area table    
    ret =UBI_Read(block_stored_UBI, page_stored_MAPII, &(tem_cached_area_table[0]), NULL);//��LEB
    
    if(ret == STATUS_SUCCESS) {
      stored_area_block=tem_index_table.area_index_table[i];//ԭ�����area table���ڵ�PEB
    
      uart_printf("%s: Read the stored %d area block from %d page of PEB %d ok\r\n",__func__,i,i,storePEB);
    }else{
      uart_printf("%s: Read the stored %d area block failed\r\n",__func__,i);
    }
    
     now_area_block=index_table.area_index_table[i];//���ڵ�area table���ڵ�PEB
    //ȷ��area blockû�б�
     if(stored_area_block==now_area_block){
       uart_printf("%s: Area %d block is the same PEB=%d\r\n",__func__,i,stored_area_block);
       ret=STATUS_SUCCESS;
     }else{       
       //ͨ���޸�INDEX_Update_Commit()������֤����area block��PEB���䣬ѭ��ʹ��
       uart_printf("%s: ERROR: Area %d block has changed, need to do more\r\n",__func__,i);       
     }
    
    
    //�ж����ڵ�aera table���ڿ��е�ҳ�Ƿ�����,��֤�����Լ���д�뱣���area table    
    if(area_offset_table[i]== PAGE_PER_PHY_BLOCK - 1){//������
      uart_printf("%s: Area table block %d is used up\r\n",__func__,i);
      //�����ڵ�area table�ȶ�����        
      ret =TABLE_Read(now_area_block, &(area_offset_table[i]), &(cached_area_table[0]));
      if(ret == STATUS_SUCCESS) {
        cached_area_number=i;//ȫ�ֱ���������cached_area_table[]�д��area table��
        uart_printf("%s: Read the now %d area block ok\r\n",__func__,i);
        ret=MTD_Erase(now_area_block);//�ٽ�area block PEB�������
        index_table.area_index_ect[i]++;
        if (ret == STATUS_SUCCESS) {//�ٽ����ڵ�area tableд��ȥ        
          ret=TABLE_Write(now_area_block, 0, &(cached_area_table[0])); 
          //��������ҳ��
         if(ret == STATUS_SUCCESS) {        
           area_offset_table[i]= 0;      
           uart_printf("%s: Rewrite %d new area table ok\r\n",__func__,i);        
         }  
       }else{//�����ֻ��飬�����������߼�
          uart_printf("%s:Warning-Bad area block in %d need to do more\r\n",__func__,i);
          ANCHOR_LogBadBlock(now_area_block);
        }    
    }     
  }
    
  //�������area tableҲд��area block�У������Ȳ��ı�area_offset_table[i]��ֵ
  //ע�⣺д�ظ���UBI table������UBI_Write��Ҫ��table_write(),д�������������� 
  if(ret == STATUS_SUCCESS) {
    ret=TABLE_Write(now_area_block, area_offset_table[i]+1, &(tem_cached_area_table[0]));
    if(ret == STATUS_SUCCESS) {
     uart_printf("%s: Rewrite %d old area table to the %d page of PEB %d ok\r\n",__func__,i,area_offset_table[i]+1,now_area_block);
    }  
   }
  
 }
  
 
  //ע��,���Ϲ���ֻ�ǽ������area table��д����area block�����ǻ�ʹ�õ������ڵ�aera table
  //�ڸ���������UBI��FTL����ٸ���area_offset_table[i]++
 
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
    
