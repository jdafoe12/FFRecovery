/*********************************************************
 * Module name: onfm.c
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
 *    Buffer writing sectors in RAM (e.g. FIFO), until
 *    enough sectors to write as an MPP (multiple plane
 *    page), which can program parallelly. Also force to
 *    flush when stop or non-seqential writing happened.
 *
 *********************************************************/

#include <core\inc\cmn.h>
#include <core\inc\buf.h>
#include <core\inc\ftl.h>
#include <core\inc\ubi.h>
#include <core\inc\mtd.h>
#include <sys\sys.h>
#include "sys\lpc313x\lib\lpc313x_chip.h"
#define RAM_DISK_SECTOR_COUNT    (EXT_SDRAM_LENGTH/SECTOR_SIZE)

#if (SIM_TEST == FALSE)
#include <drv_uart.h>
#else
#include <core\ftl\ftl_inc.h>
#endif

#include <stdio.h>

/* implement ONFM based on RAM, for bus debugging/testing */
#define ONFM_RAMDISK         (FALSE)

#if (ONFM_RAMDISK == FALSE || SIM_TEST == TRUE)

static
int onfm_read_sector(unsigned long sector_addr, void* sector_data);

static
int onfm_write_sector(unsigned long sector_addr, void* sector_data);

#if defined(__ICCARM__)
#pragma data_alignment=DMA_BURST_BYTES
#endif
static UINT8 onfm_read_buffer[MPP_SIZE];

static LSADDR read_buffer_start_sector;

SECTOR* ram_disk = (SECTOR*) (EXT_SDRAM_BASE);

/* called after failure init */
int ONFM_Format() {
  STATUS ret;
      
  MTD_Init();  
  
  ret = FTL_Format();
  if (ret == STATUS_SUCCESS) {
    return 0;
  } else {
    return -1;
  }
}

int ONFM_Capacity() {

  PGADDR page_count = FTL_Capacity() - 1;//240344
  
  uart_printf("%s: page_count=%d\r\n",__func__,page_count);
  
  int ret;
  //在FS层中以扇区sector为单位进行读写，每个扇区512字节
  //每个flash页2KB，所以提交给上层的扇区数=逻辑地址I总页数*4
  ret = page_count << SECTOR_PER_MPP_SHIFT;//<<2=*4=961376

  //jsjpenn
  uart_printf("%s: Sector number submitted=%d\r\n",__func__,ret);
  
  return ret;
}

int ONFM_Mount() {
  STATUS ret;
  read_buffer_start_sector = INVALID_LSADDR;

  BUF_Init();
  MTD_Init();
 
 
  ret = FTL_Init();
  if (ret == STATUS_SUCCESS) {
    return 0;
  } else {
    return -1;
  }
}

int ONFM_Read(unsigned long sector_addr, unsigned long sector_count,
              void* sector_data) {

  unsigned long i;
  STATUS status;
  int ret = 0;
  

  /* TODO: pre-read following page, pass back the pointer */
  if (sector_addr % SECTOR_PER_MPP == 0 && sector_count == SECTOR_PER_MPP) {
    /* read the full/aligned MPP directly, bypass the buffer read */
    status = FTL_Read(sector_addr >> SECTOR_PER_MPP_SHIFT, sector_data);
    if (status == STATUS_SUCCESS) {
      ret = 0;
    } else {
      ret = -1;
    }
  } else {
    for (i = 0; i < sector_count; i++) {
      if (ret == 0) {
        ret = onfm_read_sector(sector_addr + i,
                               ((UINT8*) sector_data) + SECTOR_SIZE * i);
      }
    }
  }
  ASSERT(ret == 0);

  return ret;
}

int ONFM_Write(unsigned long sector_addr, unsigned long sector_count,
               void* sector_data) {
  unsigned long i;
  STATUS status;
  int ret = 0;


  
  /* disable read buffer if something is written */
  read_buffer_start_sector = INVALID_LSADDR;

  if (sector_addr % SECTOR_PER_MPP == 0 && sector_count == SECTOR_PER_MPP) {
    // EXPERIMENT START
    unsigned long sec_addr = sector_addr % RAM_DISK_SECTOR_COUNT;
    if (sec_addr + sector_count >= RAM_DISK_SECTOR_COUNT) {
      sec_addr = 0;
    }
    /*sec_DRAM_addr = (void*) &(ram_disk[sec_addr][0]);
     memcpy(sec_DRAM_addr, sector_data, sector_count * SECTOR_SIZE);
     sprintf(uart_buf,"\n\rExperiment: Written sector %d at %p to %d at %p\n\r", sector_addr, sector_data, sec_addr, sec_DRAM_addr);
     UartWrite((unsigned char *)uart_buf,strlen(uart_buf));*/
    // EXPERIMENT STOP
    /* write the full/aligned MPP directly, bypass the buffer merge */
    status = FTL_Write(sector_addr >> SECTOR_PER_MPP_SHIFT, sector_data);
    
    //jsjpenn    
    if (status == STATUS_Remount) {
     //uart_printf("%s: Start to remount\r\n",__func__);
     ret=2;
     return ret;
    }//jsjpennend
    
    
            
    if (status == STATUS_SUCCESS) {
      ret = 0;
    } else {
      ret = -1;
    }
  } else {
    for (i = 0; i < sector_count; i++) {
      if (ret == 0) {
        ret = onfm_write_sector(sector_addr + i,
                                ((UINT8*) sector_data) + SECTOR_SIZE * i);
      } else {
        break;
      }
    }

    if (ret == 0) {
      /* flush the data in ram buffer */
      ret = onfm_write_sector((unsigned long) (-1), NULL);
    }
  }

  return ret;
}



//jsjpenn
extern int g_ReceiveCommandNum;
extern UINT32 g_IdleCcount;

int Start_change_state(){
  int ret; 
  
  g_ReceiveCommandNum++;    
  g_IdleCcount=0;
    
  if(g_ReceiveCommandNum==1 && g_state==1){      
    ret=FTL_UBI_StartLockImage();//过程1
    if(ret == STATUS_SUCCESS) {        
     g_state++;        
     uart_printf("%s: State 1 and process 1 done, we have entered state 2, start to test malware\r\n",__func__);       
     ret=STATUS_SUCCESS;//不需要重启
     return ret;
    }
  }

  //wpy
  if(g_ReceiveCommandNum%2==0){
    ret=FTL_UBI_RestoreImage();//过程2
    if(ret == STATUS_SUCCESS) {
     uart_printf("%s: Restore the image ok, the %d time to enter state 2,\r\n",__func__,g_ReceiveCommandNum);
     ret=STATUS_Remount; //重启
     return ret;
    }
  }  
  if(g_ReceiveCommandNum%2==1){      
    ret=FTL_UBI_StartLockImage();//过程1
    if(ret == STATUS_SUCCESS) {              
     uart_printf("%s: update the lock image ,g_state=%d,\r\n",__func__,g_state);       
     ret=STATUS_SUCCESS;//不需要重启
     return ret;
    }
  }
 return ret; 
}


int ONFM_Unmount() {
  int onfm_ret;
  STATUS ret= STATUS_SUCCESS;

   
  uart_printf("%s: USB is unmounting ...\n\r", __func__);
  
  if (ret == STATUS_SUCCESS) {
    onfm_ret = 0;
  } else {
    onfm_ret = -1;
  }

  return onfm_ret;
}

static
int onfm_read_sector(unsigned long sector_addr, void* sector_data) {
  PGADDR page_addr;
  STATUS ret = STATUS_SUCCESS;
  if (sector_addr
      >= read_buffer_start_sector&& sector_addr < read_buffer_start_sector + SECTOR_PER_MPP) {
    ; /* no need to read from FTL, just get data from the read cache */
  } else {
    page_addr = sector_addr >> SECTOR_PER_MPP_SHIFT;
    ret = FTL_Read(page_addr, onfm_read_buffer);
    if (ret == STATUS_SUCCESS) {
      read_buffer_start_sector = page_addr << SECTOR_PER_MPP_SHIFT;
    }
  }

  if (ret == STATUS_SUCCESS && sector_data != NULL) {
    memcpy(
        sector_data,
        &(onfm_read_buffer[(sector_addr - read_buffer_start_sector)
            * SECTOR_SIZE]),
        SECTOR_SIZE);

    return 0;
  } else {
    read_buffer_start_sector = INVALID_LSADDR;

    return -1;
  }
}

static
int onfm_write_sector(unsigned long sector_addr, void* sector_data) {
  static LSADDR starting_sector = INVALID_LSADDR;
  PGADDR page_addr = sector_addr >> SECTOR_PER_MPP_SHIFT;
  STATUS ret = STATUS_SUCCESS;
  void* buffer = NULL;
  if (starting_sector == INVALID_LSADDR) {
    if (sector_data != NULL) {
      starting_sector = page_addr << SECTOR_PER_MPP_SHIFT;

      /* write to buffer */
      BUF_PutSector(sector_addr, sector_data);
    } else {
      /* no data to flush. */
      ret = STATUS_SUCCESS;
    }
  } else if (sector_addr >= starting_sector&&
  sector_addr < starting_sector+SECTOR_PER_MPP &&
  sector_data != NULL) {
    /* write to buffer */
    BUF_PutSector(sector_addr, sector_data);
  } else {
    ASSERT(
        sector_data == NULL || sector_addr == starting_sector + SECTOR_PER_MPP);

    /* flush the sectors in page buffer */
    BUF_GetPage(&page_addr, &buffer);

    /* write to FTL */
    ret = FTL_Write(page_addr, buffer);
    if (ret == STATUS_SUCCESS) {
      if (sector_data != NULL) {
        /* fill buffers with next sector */
        page_addr = sector_addr >> SECTOR_PER_MPP_SHIFT;
        starting_sector = page_addr << SECTOR_PER_MPP_SHIFT;

        /* write to buffer */
        BUF_PutSector(sector_addr, sector_data);
      } else {
        ASSERT(sector_addr == (unsigned long ) (-1));
        starting_sector = INVALID_LSADDR;
      }
    }
  }

  if (ret == STATUS_SUCCESS) {
    return 0;
  } else {
    return -1;
  }
}

#else

#include "sys\lpc313x\lib\lpc313x_chip.h"

#define RAM_DISK_SECTOR_COUNT    (EXT_SDRAM_LENGTH/SECTOR_SIZE)

SECTOR* ram_disk = (SECTOR*)(EXT_SDRAM_BASE);

int ONFM_Format()
{
  memset(ram_disk, 0, RAM_DISK_SECTOR_COUNT*SECTOR_SIZE);

  return 0;
}

int ONFM_Capacity()
{
  return RAM_DISK_SECTOR_COUNT;
}

int ONFM_Mount()
{
 
  memset(ram_disk, 0, RAM_DISK_SECTOR_COUNT*SECTOR_SIZE);

  return 0;
}

int ONFM_Read(unsigned long sector_addr,
    unsigned long sector_count,
    void* sector_data)
{
  ASSERT(sector_addr+sector_count <= RAM_DISK_SECTOR_COUNT);

  memcpy(sector_data,
      &(ram_disk[sector_addr][0]),
      sector_count*SECTOR_SIZE);

  return 0;
}

int ONFM_Write(unsigned long sector_addr,
    unsigned long sector_count,
    void* sector_data)
{
  ASSERT(sector_addr+sector_count <= RAM_DISK_SECTOR_COUNT);

  /* loop to cause a slow write */
  memcpy(&(ram_disk[sector_addr][0]),
      sector_data,
      sector_count*SECTOR_SIZE);

  BUF_Free(sector_data);

  return 0;
}

int ONFM_Unmount()
{
  return 0;
}

#endif
