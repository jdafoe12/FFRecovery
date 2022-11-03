/*********************************************************
 * Module name: ftl_inc.h
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
 *    FTL data structures. 
 *
 *********************************************************/

#ifndef _FTL_INC_H_
#define _FTL_INC_H_

/* Reserve two lowest bit, so the buffer pointed by address 
 * should be aligned with 4-byte.
 * - bit 0: 0-in RAM, 1-in NAND;
 * - bit 1: 0-clean,  1-dirty;
 */
typedef UINT32 PM_NODE_ADDR;

#define PM_NODE_IS_CACHED(p)  (((p)&0x1) == 0)
#define PM_NODE_IS_DIRTY(p)   (((p)&0x2) != 0)
#define PM_NODE_ADDRESS(p)    ((PM_NODE_ADDR*)((p)&(~(0x3))))
#define PM_NODE_BLOCK(p)      (((p)>>2)>>PAGE_PER_BLOCK_SHIFT)
#define PM_NODE_PAGE(p)       (((p)>>2)&((1<<PAGE_PER_BLOCK_SHIFT)-1))
#define PM_NODE_SET_DIRTY(p)  ((p) |= 0x2)
#define PM_NODE_SET_BLOCKPAGE(p, blk, page)        \
                     ((p) = ((((blk)<<PAGE_PER_BLOCK_SHIFT)+(page))<<2) + 1)
#define INVALID_PM_NODE       ((PM_NODE_ADDR)(-1))

#if (PAGE_PER_BLOCK_SHIFT <= 8)
typedef UINT8 DIRTY_PAGE_COUNT;
#else
#error "large block is not supported!"
#endif

#define JOURNAL_BLOCK_COUNT         (TOTAL_DIE_COUNT)//1
#define PM_PER_NODE                 (MPP_SIZE/sizeof(PM_NODE_ADDR))//512

#define CLUSTER_INDEX(pa)           ((pa)/PM_PER_NODE)
#define PAGE_IN_CLUSTER(pa)         ((pa)%PM_PER_NODE)

//����
#define BDT_BLOCK0         (0)
#define BDT_BLOCK1         (1)

#define ROOT_BLOCK0        (2)
#define ROOT_BLOCK1        (3)

#define HDI_BLOCK0         (4)
#define HDI_BLOCK1         (5)

#define PMT_START_BLOCK    (6)
/* TODO: shrink PMT size, by removing PMT of continous pages */
#define PMT_BLOCK_COUNT    (((CFG_LOG_BLOCK_COUNT+PM_PER_NODE-1)/PM_PER_NODE) * 5)//40

//jsjpenn
#define PMTRESORE_START_BLOCK (PMT_START_BLOCK+PMT_BLOCK_COUNT)//46
#define PMTRESORE_BLOCK_COUNT (8)//Ϊ����MAPI��Ԥ��8��LEB��

#define FTL_reserved_START_BLOCK  (PMTRESORE_START_BLOCK+PMTRESORE_BLOCK_COUNT)//54
#define FTL_reserved_BLOCK_COUNT  (1)//Ϊ����FTL�ĸ�������MAP I���⣩��Ԥ��1��LEB��


#define UBI_reserved_START_BLOCK (FTL_reserved_START_BLOCK+FTL_reserved_BLOCK_COUNT)//55
#define UBI_reserved_BLOCK_COUNT (1)//Ϊ����UBI�ĸ��������� MAP II����Ԥ��1��LEB��

#define DATA_START_BLOCK   (UBI_reserved_START_BLOCK+UBI_reserved_BLOCK_COUNT)//56
#define DATA_LAST_BLOCK    (UBI_Capacity-1)//3991-1=3990

#define MAX_DIRTY_PAGES    (PAGE_PER_PHY_BLOCK-1)//63
#define MAX_PM_CLUSTERS    (MPP_SIZE/sizeof(UINT32)-(JOURNAL_BLOCK_COUNT*3+6))//512-9=503
                                                                          //��ROOT TABLE���ݽṹȷ��

typedef PM_NODE_ADDR JOURNAL_ADDR;
typedef PM_NODE_ADDR PM_NODE[PM_PER_NODE];

typedef struct {
  /* DATA journal */
  JOURNAL_ADDR hot_journal[JOURNAL_BLOCK_COUNT];
  JOURNAL_ADDR cold_journal[JOURNAL_BLOCK_COUNT];
  JOURNAL_ADDR reclaim_journal[JOURNAL_BLOCK_COUNT];

  /* PMT journal */
  JOURNAL_ADDR pmt_current_block;
  JOURNAL_ADDR pmt_reclaim_block;

  /* HDI journal */
  JOURNAL_ADDR hdi_current_journal;

  /* BDT journal */
  JOURNAL_ADDR bdt_current_journal;

  /* ROOT journal */
  JOURNAL_ADDR root_current_journal;

  /* root edition */
  UINT32 root_edition;

  /* PMT nodes: hold all the remaining space in a page */
  PM_NODE_ADDR page_mapping_nodes[MAX_PM_CLUSTERS]; // stores the address of the cluster?
} ROOT;

extern ROOT root_table;
extern DIRTY_PAGE_COUNT block_dirty_table[];


//jsjpenn
extern int g_state;
extern int g_ReceiveCommandNum;
extern UINT32 g_IdleCcount;

//JD 
extern int ftl_read_state;



/*********************************************************
 * Funcion Name: BDT_Format
 *
 * Description:
 *    Format the BDT block with initial data.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS BDT_Format();

/*********************************************************
 * Funcion Name: BDT_Init
 *
 * Description:
 *    Read the BDT from BDT blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS BDT_Init();

/*********************************************************
 * Funcion Name: BDT_Commit
 *
 * Description:
 *    Update BDT to the blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS BDT_Commit();

/*********************************************************
 * Funcion Name: DATA_Format
 *
 * Description:
 *    Format data journal blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS DATA_Format();

/*********************************************************
 * Funcion Name: DATA_Write
 *
 * Description:
 *    Write data to journals.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    addr     IN    logical page address to write
 *    buffer   IN    the data to write
 *    is_hot   IN    hot data flag
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS DATA_Write(PGADDR addr, void* buffer, BOOL is_hot);

/*********************************************************
 * Funcion Name: DATA_Commit
 *
 * Description:
 *    Commit data journals with updating PMT/ROOT ...
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS DATA_Commit();

/*********************************************************
 * Funcion Name: DATA_IsFull
 *
 * Description:
 *    Check if any data journal is full.
 *
 * Return Value:
 *    BOOL        true if full
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
BOOL DATA_IsFull();

/*********************************************************
 * Funcion Name: DATA_Reclaim
 *
 * Description:
 *    Choose dirtiest data blocks, and copy valid data to 
 *    reclaim blocks. 
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS DATA_Reclaim();

/*********************************************************
 * Funcion Name: BDT_Commit
 *
 * Description:
 *    Replay data in journal blocks to recover context.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    journals    IN    the journal to replay
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS DATA_Replay(JOURNAL_ADDR* journals);

/*********************************************************
 * Funcion Name: HDI_Format
 *
 * Description:
 *    Format HDI blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS HDI_Format();

/*********************************************************
 * Funcion Name: HDI_Init
 *
 * Description:
 *    Read HDI from the blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS HDI_Init();

/*********************************************************
 * Funcion Name: HDI_IsHotPage
 *
 * Description:
 *    Check if the data to read is hot or cold.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    addr     IN    the logical address of the page to write
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
BOOL HDI_IsHotPage(PGADDR addr);

/*********************************************************
 * Funcion Name: HDI_Commit
 *
 * Description:
 *    Update HDI to the blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS HDI_Commit();

/*********************************************************
 * Funcion Name: PMT_Format
 *
 * Description:
 *    Format PMT blocks
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS PMT_Format();

/*********************************************************
 * Funcion Name: PMT_Init
 *
 * Description:
 *    Load PMT from the blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS PMT_Init();

/*********************************************************
 * Funcion Name: PMT_Update
 *
 * Description:
 *    Update the location of the logical page in PMT index.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    page_addr      IN    the logical page address
 *    block          IN    new logical block address
 *    page           IN    new page offset in the block
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS PMT_Update(PGADDR page_addr, LOG_BLOCK block, PAGE_OFF page);

/*********************************************************
 * Funcion Name: PMT_Search
 *
 * Description:
 *    Find the location of the logical page
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    page_addr      IN    the logical page address
 *    block          OUT   valid logical block address
 *    page           OUT   valid page offset in the block
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS PMT_Search(PGADDR logcial_addr, LOG_BLOCK* block, PAGE_OFF* page);

/*********************************************************
 * Funcion Name: PMT_Load
 *
 * Description:
 *    Read PMT page from blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    block          IN    block address of the PMT page
 *    page           IN    page offset of the PMT page
 *    cluster        IN    the cluster number of the PMT page
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS PMT_Load(LOG_BLOCK block, PAGE_OFF page, PMT_CLUSTER cluster);

/*********************************************************
 * Funcion Name: PMT_Commit
 *
 * Description:
 *    Write back all updated PMT pages to blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS PMT_Commit();

/*********************************************************
 * Funcion Name: ROOT_Format
 *
 * Description:
 *    Format ROOT blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS ROOT_Format();

/*********************************************************
 * Funcion Name: ROOT_Init
 *
 * Description:
 *    Read ROOT from blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS ROOT_Init();

/*********************************************************
 * Funcion Name: ROOT_Commit
 *
 * Description:
 *    Update ROOT to blocks.
 *
 * Return Value:
 *    STATUS      F/S
 *
 * Parameter List:
 *    N/A
 *
 * NOTES:
 *    N/A
 *
 *********************************************************/
STATUS ROOT_Commit();

#endif

//jsjpenn
STATUS FTL_HDI_hash_table_store();
STATUS FTL_BDT_table_store();
STATUS FTL_ROOT_table_store(); 

STATUS FTL_HDI_hash_table_restore();
STATUS FTL_BDT_table_restore();
STATUS FTL_ROOT_table_restore();

STATUS Truly_FTL_HDI_hash_table_restore();
STATUS Truly_FTL_BDT_table_restore();
STATUS Truly_FTL_ROOT_table_restore();