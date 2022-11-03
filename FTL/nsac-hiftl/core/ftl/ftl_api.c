/*********************************************************
 * Module name: ftl_api.c
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
 *    FTL APIs.
 *
 *********************************************************/

#include <core\inc\cmn.h>
#include <core\inc\ftl.h>
#include <core\inc\ubi.h>
#include <core\inc\mtd.h>
#include <sys\sys.h>
#include "ftl_inc.h"
#include <core\inc\buf.h>


/* Advanced Page Mapping FTL:
 * - Block Dirty Table: LOG_BLOCK 0, cache all
 * - ROOT Table: LOG_BLOCK 1, cache all. point to journal blocks.
 * - Page Mapping Table: LOG_BLOCK 2~N, cache x pages with LRU algo.
 * - DATA Journal: commit
 * - Init: read BDT, ROOT, PMT, Journal info, ...
 * - Reclaim
 * - Meta Data Page: in last page in PMT blocks and data blocks.
 * - choose journal block on erase and write, according to die index
 *
 * TODO: advanced features:
 * - sanitizing
 * - bg erase
 * - check wp/trim, ...
 */
   
 //jsjpenn
UINT32 g_LBAnum;//逻辑地址I总数 
   

STATUS FTL_Format() {
    
  STATUS ret;    
  ret = UBI_Format();
  if (ret == STATUS_SUCCESS) {
    ret = UBI_Init();
  }

  if (ret == STATUS_SUCCESS) {
    ret = DATA_Format();
  }

  if (ret == STATUS_SUCCESS) {
    ret = HDI_Format();
  }

  if (ret == STATUS_SUCCESS) {
    ret = PMT_Format();
  }

  if (ret == STATUS_SUCCESS) {
    ret = BDT_Format();
  }

  if (ret == STATUS_SUCCESS) {
    ret = ROOT_Format();
  }

  return ret;
}

STATUS FTL_Init() {
  STATUS ret;

  ret = UBI_Init();
  if (ret == STATUS_SUCCESS) {
    /* scan tables on UBI, and copy to RAM */
    ret = ROOT_Init();
  }

  if (ret == STATUS_SUCCESS) {
    ret = BDT_Init();
  }

  if (ret == STATUS_SUCCESS) {
    ret = PMT_Init();
  }

  if (ret == STATUS_SUCCESS) {
    ret = HDI_Init();
  }
    
  //jsjpenn在状态2中更新hot 与cold日志块
  if (ret == STATUS_SUCCESS && g_state==2) {
    JOURNAL_ADDR* journal;
    JOURNAL_ADDR* exclude_journal;
    journal = root_table.hot_journal;
    exclude_journal = root_table.cold_journal;
    UINT32 hotblock, coldblock;
    hotblock = PM_NODE_BLOCK(journal[0]);
    coldblock= PM_NODE_BLOCK(exclude_journal[0]);
    //uart_printf("%s: Before change: hot journal blcok=%d,cold journal blcok=%d\r\n",__func__,hotblock,coldblock);
    
    ret = DATA_Reclaim(TRUE);//更新hot日志块
    if (ret == STATUS_SUCCESS) {
      ret = DATA_Reclaim(FALSE);//更新cold日志块
      if (ret == STATUS_SUCCESS) {
        ret = DATA_Commit();//更新FTL的各个表到flash上
        if (ret == STATUS_SUCCESS) {
          hotblock = PM_NODE_BLOCK(journal[0]);
          coldblock= PM_NODE_BLOCK(exclude_journal[0]);  
          //uart_printf("%s: After change: hot journal blcok=%d,cold journal blcok=%d,\r\n",__func__,hotblock,coldblock);        
        }
     }
    }  
  }
  
  //jsjpenn
  //只在状态1中执行即可，状态2时从新的日志块开始，且内存中的映射已清空
 if (g_state==1) {
   
  if (ret == STATUS_SUCCESS) { 
 
    ret = DATA_Replay(root_table.hot_journal);
  }

  if (ret == STATUS_SUCCESS) {
   
    ret = DATA_Replay(root_table.cold_journal);
  }

  if (ret == STATUS_SUCCESS) {
    /* handle reclaim PLR: start reclaim again. Some data should
     * be written in the same place, so just rewrite same data in the
     * same page regardless this page is written or not. */

    /* check if hot journal blocks are full */
    if (DATA_IsFull(TRUE) == TRUE) {
      ret = DATA_Reclaim(TRUE);
      if (ret == STATUS_SUCCESS) {
        ret = DATA_Commit();
      }
    }

    /* check if cold journal blocks are full */
    if (DATA_IsFull(FALSE) == TRUE) {
      ret = DATA_Reclaim(FALSE);
      if (ret == STATUS_SUCCESS) {
        ret = DATA_Commit();
      }
    }
  }
  
  
 }//jsjpennend
 
 
  return ret;
}


int tag=0;

STATUS FTL_Write(PGADDR addr, void* buffer) {
  STATUS ret;
  //uart_printf("%s: LBA=%d\r\n",__func__,addr); 
   
  //jsjpenn 判断是否需要改变状态g_LBAnum=240345
  //if(addr==g_LBAnum-100){//240245 linux判断扇区数=240245*4=960980 
  
//    if(addr>g_LBAnum){
//      tag++;
//      uart_printf("***%s***: tag=%d\r\n",__func__,tag);
//      
//      if(tag>1){//在格式化文件系统时会触发一次
//        g_ReceiveCommandNum++;
//        
//        //Lock: sudo dd if=/dev/zero of=/dev/sdb bs=512 count=1 seek=961384
//        if(addr==240346){          
//          //if(g_ReceiveCommandNum==1 && g_state==1){      
//            ret=FTL_UBI_StartLockImage();//过程1
//            if(ret == STATUS_SUCCESS) {        
//             g_state++;        
//             uart_printf("%s: State 1 and process 1 done, we have entered state 2, start to test malware\r\n",__func__);        
//             ret=STATUS_SUCCESS;//不需要重启
//             return ret;
//            }
//          //}         
//        }
//        
//       //Recover: sudo dd if=/dev/zero of=/dev/sdb bs=512 count=1 seek=961388  
//       if(addr==240347){
//         //if(g_ReceiveCommandNum>=2){
//          ret=FTL_UBI_RestoreImage();//过程2
//          if(ret == STATUS_SUCCESS) {
//           uart_printf("%s: Restore the image ok, the %d time to enter state 2,\r\n",__func__,g_ReceiveCommandNum);
//           ret=STATUS_Remount;             
//           return ret;
//          }
//        //}         
//       }
//       return ret=STATUS_SUCCESS;   
//        
//      }      
//      
//    }
     
  
    /****************************nc test****************/
    if(addr==237846){ //951384         
          //if(g_ReceiveCommandNum==1 && g_state==1){      
            ret=FTL_UBI_StartLockImage();//过程1
            if(ret == STATUS_SUCCESS) {        
             g_state++;        
             uart_printf("%s: State 1 and process 1 done, we have entered state 2, start to test malware\r\n",__func__);        
             ret=STATUS_SUCCESS;//不需要重启        
             return ret;
            }
            
            else {
              uart_printf("state 1 failed\n");
            }
          //}         
        }
    
    if(addr==237847){ //951388
         //if(g_ReceiveCommandNum>=2){
          ret=FTL_UBI_RestoreImage();//过程2
          if(ret == STATUS_SUCCESS) {
           uart_printf("%s: Restore the image ok, the %d time to enter state 2,\r\n",__func__,g_ReceiveCommandNum);
           ret=STATUS_Remount;             
           return ret;
          }
          else {
            uart_printf("state 2 failed\n");
          }
        //}         
       }
    
    /****************************nc test****************/
    
    // JD
    if(addr==237848) { // 951392
      if(ftl_read_state == 0) {
        ret = STATUS_SUCCESS;
        ftl_read_state = 1;
        uart_printf("%Entered read state: %d - Recovery\n", ftl_read_state);
      }
      else {
        ret = STATUS_SUCCESS;
        ftl_read_state = 0;
        uart_printf("Entered read state: %d - Normal\n", ftl_read_state);
        
      }
      return ret;
    }
    
    // JD end
    
  BOOL is_hot = HDI_IsHotPage(addr);  
   
  ret = DATA_Write(addr, buffer, is_hot);
  
  if (ret == STATUS_SUCCESS) {
    if (DATA_IsFull(is_hot) == TRUE) {
      ret = DATA_Reclaim(is_hot);
      if (ret == STATUS_SUCCESS) {
        ret = DATA_Commit();
      }
    }
  }
  return ret;
}

STATUS FTL_Read(PGADDR addr, void* buffer) {
  LOG_BLOCK block;
  PAGE_OFF page;
  STATUS ret;
  
  //uart_printf("%s: LBA=%d\r\n",__func__,addr); 
  
  ret = PMT_Search(addr, &block, &page);
  //uart_printf("block: %d page: %d\n", block, page);
  if (ret == STATUS_SUCCESS) {
    ret = UBI_Read(block, page, buffer, NULL);
  }
  return ret;
}

STATUS FTL_Trim(PGADDR start, PGADDR end) {
  PGADDR addr;
  STATUS ret = STATUS_SUCCESS;

  for (addr = start; addr <= end; addr++) {
    ret = FTL_Write(addr, NULL);
    if (ret != STATUS_SUCCESS) {
      break;
    }
  }

  return ret;
}

STATUS FTL_SetWP(PGADDR laddr, BOOL enabled) {
  return STATUS_FAILURE;
}

BOOL FTL_CheckWP(PGADDR laddr) {
  return FALSE;
}

STATUS FTL_BgTasks() {
  return STATUS_SUCCESS;
}

PGADDR FTL_Capacity() {
  LOG_BLOCK block;

  block = UBI_Capacity;//3991
  block -= JOURNAL_BLOCK_COUNT; /* data hot journal *///1
  block -= JOURNAL_BLOCK_COUNT; /* data cold journal *///1
  block -= JOURNAL_BLOCK_COUNT; /* data reclaim journal *///1
  block -= PMT_BLOCK_COUNT; /* pmt blocks *///40
  block -= 2; /* bdt blocks */
  block -= 2; /* root blocks */
  block -= 2; /* hdi reserved */
  
  //jsjpenn
  block -= 8;//PMTRESORE_START_BLOCK
  block -= 1;//UBI_reserved_START_BLOCK
  block -=1;//FTL_reserved_START_BLOCK
  
  block -= block / 100 * OVER_PROVISION_RATE; /* over provision */
    
  //jsjpenn
   g_LBAnum=block * (PAGE_PER_PHY_BLOCK - 1);//逻辑地址I总数=3815*63=240345 
   
  /* last page in every block is reserved for meta data collection */
  return g_LBAnum;//返回逻辑地址I总数
}

STATUS FTL_Flush() {
  STATUS ret= STATUS_SUCCESS;
    
 
  if (ret == STATUS_SUCCESS) {
    ret = UBI_Flush();
  }

  if (ret == STATUS_SUCCESS) {
    
  }

  return ret;
}


//jsjpenn
STATUS FTL_UBI_StartLockImage(){

  STATUS ret= STATUS_SUCCESS;  
  //uart_printf("%s: Start to lock image\r\n",__func__);   
  
   //wpy 
  UINT32 i;
  LOG_BLOCK block =PMTRESORE_START_BLOCK;//46
  
 //wpy
  for(i=0;i<PMTRESORE_BLOCK_COUNT+FTL_reserved_BLOCK_COUNT;i++){ // MAPII stored in 55-71. Should these be erased?
    if (ret == STATUS_SUCCESS) {
      ret = UBI_Erase(block, block);//擦除存储的前一个状态的各种表 46-54，UBI层不变
    }
    block++;
  }
  
  //1.更新hot、cold日志块，保证image与malware不共用块
  JOURNAL_ADDR* journal;
  JOURNAL_ADDR* exclude_journal;
  journal = root_table.hot_journal;
  exclude_journal = root_table.cold_journal;
 
  //将日志块中的MAPI更新到flash上的PMT块中，保证SRAM中的映射更新到了flash上
  if (ret == STATUS_SUCCESS) {
    ret = DATA_Replay(journal);
  }
  if (ret == STATUS_SUCCESS) {
    ret = DATA_Replay(exclude_journal);
  }  
  
 
  
  ret = DATA_Reclaim(TRUE);//更新hot日志块
  if (ret == STATUS_SUCCESS) {
    ret = DATA_Reclaim(FALSE);//更新cold日志块
    if (ret == STATUS_SUCCESS) {
      ret = DATA_Commit();//此韩式真正实现将FTL的各个表更新到flash上
      if (ret == STATUS_SUCCESS) {
             
      }
    }
  }  
   
  /*  
  //2.保存UBI各种表结构
  //由于改变了UBI_Erase()中的逻辑，MAPII不会改变（除非遇到坏块和使用均衡）
  //因此不用保存UBI的各个表
  ret=UBI_table_store();  
  if(ret == STATUS_SUCCESS) {    
    uart_printf("%s: Write all the UBI tables ok\r\n",__func__);
  }*/
  
  
  //3.保存FTL各种表结构
  ret=FTL_table_store();  
  if(ret == STATUS_SUCCESS) {    
    //uart_printf("%s: Write all the FTL tables ok\r\n",__func__);     
  }
  
  return ret;
}

//2 保存UBI各种表结构
STATUS UBI_table_store(){

  STATUS ret;
  
  //1.保存AREA table，即MAP II
    ret=UBI_MAPII_store();

 //2.保存INDEX table
   if(ret == STATUS_SUCCESS) {
    ret=UBI_INDEX_table_store();    
  }    
    
 //3.保存ANCHOR table 
   if(ret == STATUS_SUCCESS) {
     ret=UBI_ANCHOR_table_store(); 
   }
   
  return ret;
}



//3 保存FTL各种表结构
STATUS FTL_table_store(){
   
  STATUS ret;
  
  //1.保存HDI hash table
  ret=FTL_HDI_hash_table_store();
  
 //2.保存MAPI
 if(ret == STATUS_SUCCESS) {
  FTL_MAPI_store();
 }  
  
 //3.保存BDT table
 if(ret == STATUS_SUCCESS) {
  FTL_BDT_table_store();
 }
  
 //4.保存ROOT table
 if(ret == STATUS_SUCCESS) {
  FTL_ROOT_table_store();
 } 
   
  return ret;  
}


//3.2保存MAPI
STATUS FTL_MAPI_store(){  
  STATUS ret = STATUS_SUCCESS;
  UINT32 iClusterNum;//总簇数
  UINT32 i,j,k;  
  
  LOG_BLOCK block_to_write;
  PAGE_OFF page_to_write;
  
  LOG_BLOCK block;
  PAGE_OFF page;
  
  PM_NODE_ADDR iPM_NODE[PM_PER_NODE];//512
  PM_NODE_ADDR* cache_addr = NULL;
  
  SPARE spare; 
  
  LOG_BLOCK LEBblock;
  LOG_BLOCK LEBtemp;
  
  LOG_BLOCK ValidLEBTable[4096];
  UINT32  ValidLEBNum=0;
  
  UINT32 judge=1;
  
      
  //MAPI占8块，存到逻辑II的8个LEB块中   
  iClusterNum=((g_LBAnum + PM_PER_NODE - 1) /  PM_PER_NODE);//总簇数    470
  cache_addr = &(iPM_NODE[0]);
  
  for(i=0;i<iClusterNum;i++){ // Loops 470 times
        
      block_to_write=(i/PAGE_PER_PHY_BLOCK)+PMTRESORE_START_BLOCK;//从LEB 46块开始  46-53
      page_to_write=i%PAGE_PER_PHY_BLOCK;
     // uart_printf("block to write in MAPI store: %d page to write in MAPI Store: %d\n", block_to_write, page_to_write);
      
      block = PM_NODE_BLOCK(root_table.page_mapping_nodes[i]);
      //uart_printf("associated pm node block: %d\n", block);
      page = PM_NODE_PAGE(root_table.page_mapping_nodes[i]);
      //uart_printf("associated pm node page: %d\n", page);
      ret = UBI_Read(block, page, cache_addr, NULL);
      
      if(ret == STATUS_SUCCESS) {
        spare[0]=i; //簇号     
        //uart_printf("spare[0]: %d\n", spare[0]);
        ret = UBI_Write(block_to_write, page_to_write, cache_addr, spare, FALSE);        
      }
            
      
  }  
  
  
   return ret;  
}


//恢复image
STATUS FTL_UBI_RestoreImage(){
  STATUS ret= STATUS_SUCCESS;
  
  /*
  //1.恢复UBI各种表结构
  ret=UBI_table_restore();   
  if(ret == STATUS_SUCCESS){    
    uart_printf("%s: Rewrite all the UBI tables ok\r\n",__func__);
  }*/
  
  //2.恢复FTL各种表结构
  ret=FTL_table_restore();  
  if(ret == STATUS_SUCCESS){    
   // uart_printf("%s: Rewrite all the FTL tables ok\r\n",__func__);     
  }
  
  /*
  //3.真正更新UBI各种表结构
  ret=Truly_UBI_table_restore();  
  if(ret == STATUS_SUCCESS){    
    uart_printf("%s: Truly restore all the UBI tables ok\r\n",__func__);     
  }*/
  
  /*
  //4.真正更新FTL各种表结构-无实际作用
   ret=Truly_FTL_table_restore();  
  if(ret == STATUS_SUCCESS){    
    uart_printf("%s: Truly restore all the FTL tables ok\r\n",__func__);     
  }*/
  
  
 return ret;  
}


STATUS UBI_table_restore(){
  
    STATUS ret;  
        
    
  //1.恢复AREA table，即MAP II
   ret=UBI_MAPII_restore();
    
    
  //2.恢复INDEX table    
  if(ret == STATUS_SUCCESS){
   ret=UBI_INDEX_table_restore();    
  }
  
   //3.恢复ANCHOR table 
  if(ret == STATUS_SUCCESS){    
    ret=UBI_ANCHOR_table_restore(); 
  }


  return ret;  
}

STATUS FTL_table_restore(){
  
    STATUS ret;

    //1.恢复HDI hash table
    ret=FTL_HDI_hash_table_restore(); 
   
    if(ret == STATUS_SUCCESS) {
     //1.真正恢复HDI hash table-更新了新的root table
     ret=Truly_FTL_HDI_hash_table_restore();  
    }

   //2.恢复MAPI—-更新了新的root table，更新了PMT部分的BDT
   if(ret == STATUS_SUCCESS) {
     FTL_MAPI_restore();
   }
   
   /*
   //2.真正恢复MAPI—没有操作
   if(ret == STATUS_SUCCESS) {
     ret=Truly_FTL_MAPI_restore();
   }*/
 
 
  
    //3.恢复BDT table
   if(ret == STATUS_SUCCESS) {
     FTL_BDT_table_restore();
   }
    //3.真正恢复BDT table--更新了新的root table
   if(ret == STATUS_SUCCESS) {
    ret=Truly_FTL_BDT_table_restore();
   } 
   

   //4.恢复ROOT table 
    if(ret == STATUS_SUCCESS) {  
      ret=FTL_ROOT_table_restore();
   }

    /*
    //4.真正恢复ROOT table—没有操作 
    if(ret == STATUS_SUCCESS) {  
      ret=Truly_FTL_ROOT_table_restore();
   } */

 return ret;
}



STATUS Truly_UBI_table_restore(){
  
  STATUS ret; 
  
  
  //0.维护恢复前（LEB中的除data block区）的其他部分对应的PEB 
  UINT8 i;  
  PHY_BLOCK peb_before[DATA_START_BLOCK]; //56 
  PHY_BLOCK peb_after[DATA_START_BLOCK]; //56 
  for(i=0;i<DATA_START_BLOCK;i++){
    peb_before[i]=AREA_GetBlock(i);//LEB对应PEB
  }
  
  
  //1.恢复AREA table，即MAP II
   ret=Truly_UBI_MAPII_restore();
  
    
  //2.恢复INDEX table    
  if(ret == STATUS_SUCCESS){
   ret=Truly_UBI_INDEX_table_restore();    
  }
  
   //3.恢复ANCHOR table 
  if(ret == STATUS_SUCCESS){    
    ret=Truly_UBI_ANCHOR_table_restore(); 
  }
  
  //判断0-55的LEB所对应的PEB不变
  for(i=0;i<DATA_START_BLOCK;i++){
    peb_after[i]=AREA_GetBlock(i);//LEB对应PEB
    if(peb_after[i]==peb_before[i]){
      //uart_printf("%s: The PEB of LEB %d not change\r\n",__func__,i);
    }
  }
  
  
  return ret;  
}


STATUS Truly_FTL_table_restore(){
  STATUS ret= STATUS_SUCCESS;  
 
  return ret; 
}

//恢复MAP I
#define PMT_CURRENT_BLOCK  (PM_NODE_BLOCK(root_table.pmt_current_block))
#define PMT_CURRENT_PAGE   (PM_NODE_PAGE(root_table.pmt_current_block))
#define PMT_RECLAIM_BLOCK  (PM_NODE_BLOCK(root_table.pmt_reclaim_block))
#define PMT_RECLAIM_PAGE   (PM_NODE_PAGE(root_table.pmt_reclaim_block))

extern STATUS pmt_reclaim_blocks();

STATUS FTL_MAPI_restore(){  
  STATUS ret = STATUS_SUCCESS;
  UINT32 iClusterNum;//总簇数
  
  PM_NODE_ADDR iPM_NODE[PM_PER_NODE];//512
  PM_NODE_ADDR* cache_addr = NULL; 
  
  LOG_BLOCK block_stored;
  PAGE_OFF page_stored;  
  SPARE spare; 
  
  PMT_CLUSTER meta_data[PAGE_PER_PHY_BLOCK];//保存1个PMT块中写入的簇号
  
  UINT32 i,j; 
  
  PMT_CLUSTER pm_cluster;  
  
  //将PMT区中各LEB的BDT全部设为63,全脏，以便回收PMT中的块
  for (i = PMT_START_BLOCK; i < PMT_START_BLOCK + PMT_BLOCK_COUNT; i++) {
    block_dirty_table[i] = MAX_DIRTY_PAGES;//63 
  }  
  pmt_reclaim_blocks();//对PMT块区中的无效的映射进行回收，更新pmt的current块，保证从新的PMT块恢复MAPI
  
  
  //MAPI占8块，存到逻辑II的8个LEB块中   
  iClusterNum=((g_LBAnum + PM_PER_NODE - 1) / PM_PER_NODE);//总簇数471   
  cache_addr = &(iPM_NODE[0]);
  PMT_Init();//将内存中的映射清空
  
  for(i=0;i<iClusterNum;i=i+PAGE_PER_PHY_BLOCK-1){      
      
    for(j=0;j<PAGE_PER_PHY_BLOCK-1;j++){//j<63        
      pm_cluster=i+j; 
      //已保存的MAPI的位置
      block_stored=(pm_cluster/PAGE_PER_PHY_BLOCK)+PMTRESORE_START_BLOCK;//从LEB46块开始
      page_stored=pm_cluster % PAGE_PER_PHY_BLOCK;
      
      ret = UBI_Read(block_stored, page_stored, cache_addr, spare);
      
      if (ret == STATUS_SUCCESS) {     
       
        ret = UBI_Write(PMT_CURRENT_BLOCK, PMT_CURRENT_PAGE,cache_addr, spare, FALSE);
        if (ret == STATUS_SUCCESS) {
          meta_data[PMT_CURRENT_PAGE] = pm_cluster;
         
        }                 
        PM_NODE_SET_BLOCKPAGE(root_table.page_mapping_nodes[pm_cluster],PMT_CURRENT_BLOCK, PMT_CURRENT_PAGE);
        PM_NODE_SET_BLOCKPAGE(root_table.pmt_current_block, PMT_CURRENT_BLOCK, PMT_CURRENT_PAGE+1);
      }        
    }
    //每写63页MAP I后，在PMT的最后一块中写入该块中已写入对应的簇号
    if (ret == STATUS_SUCCESS) {
      ret = UBI_Write(PMT_CURRENT_BLOCK, PMT_CURRENT_PAGE, meta_data, NULL, FALSE);
      if (ret == STATUS_SUCCESS) {              
        ret = pmt_reclaim_blocks();//更新pmt的current块，从新的PMT块继续恢复MAPI
      }       
    }     
  }
  
  //设置root_table.pmt_reclaim_block
  if (ret == STATUS_SUCCESS) {
     ret = UBI_Erase(PMT_CURRENT_BLOCK+1, PMT_CURRENT_BLOCK+1);
   }
  if (ret == STATUS_SUCCESS) {
     PM_NODE_SET_BLOCKPAGE(root_table.pmt_reclaim_block, PMT_CURRENT_BLOCK+1, 0);
     block_dirty_table[PMT_CURRENT_BLOCK+1] = 0;
  }
  
  //todo：是否需要在最后1个PMT块（未用完）的最后1页写入已写入的簇号？  
  
  return ret; 
}


STATUS Truly_FTL_MAPI_restore(){
 STATUS ret = STATUS_SUCCESS;
 return ret; 
}
