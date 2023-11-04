/*********************************************************
 * Module name: ftl_pmt.c
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
 * Updated by vinay.g.jain@gmail.com on Nov 22 2014.
 *
 * Module Description:
 *    Page Mapping Table. It contains 2 layers of table. 
 *    The first layer is ROOT, and points to every second
 *       layer of PMT (aka. CLUSTER)
 *    The second layer is PMT pages, and holding logical 
 *       page mapping info, pointing to UBI block/page.
 *
 *********************************************************/

#include <core\inc\cmn.h>
#include <core\inc\ftl.h>
#include <core\inc\ubi.h>
#include <sys\sys.h>
#include "ftl_inc.h"

#define PMT_CURRENT_BLOCK  (PM_NODE_BLOCK(root_table.pmt_current_block))
#define PMT_CURRENT_PAGE   (PM_NODE_PAGE(root_table.pmt_current_block))
#define PMT_RECLAIM_BLOCK  (PM_NODE_BLOCK(root_table.pmt_reclaim_block))
#define PMT_RECLAIM_PAGE   (PM_NODE_PAGE(root_table.pmt_reclaim_block))

#if defined(__ICCARM__)
/* must be aligned to 4bytes, because the lowest 2 bits is reserved */
#pragma data_alignment=4
#endif
static PM_NODE pm_node_caches[PMT_CACHE_COUNT];
static PM_NODE_ADDR pm_cache_origin_location[PMT_CACHE_COUNT];
static PMT_CLUSTER pm_cache_cluster[PMT_CACHE_COUNT];
/* meta data in last page */
static PMT_CLUSTER meta_data[PAGE_PER_PHY_BLOCK];
/* buffer used in reclaim */
static PMT_CLUSTER clusters[MPP_SIZE / sizeof(PMT_CLUSTER)];
static UINT8 pm_node_buffer[MPP_SIZE];


//jsjpenn

// JD
UINT8 invalid_time[3991] = {0};

// end JD

extern  STATUS pmt_reclaim_blocks();//为了能在ftl_api.c中被调用


STATUS PMT_Format() {
  LOG_BLOCK pmt_block = PMT_START_BLOCK;
  PAGE_OFF pmt_page = 0;
  PM_NODE pm_node;
  STATUS ret = STATUS_SUCCESS;
  SPARE spare;
  UINT32 i;
  UINT32 j;
  UINT32 pmt_cluster_count = ((FTL_Capacity() + PM_PER_NODE - 1) /  PM_PER_NODE);//总簇数471
  /* root table has enough space to hold 1st level of pmt */
  ASSERT(pmt_cluster_count < MAX_PM_CLUSTERS);

  for (i = 0; i < pmt_cluster_count; i++) {
    if (ret == STATUS_SUCCESS) {
      /* format a cluster of PMT */
      for (j = 0; j < PM_PER_NODE; j++) {//512
        pm_node[j] = INVALID_PM_NODE;
      }
      spare[0] = i;//簇号
      ret = UBI_Write(pmt_block, pmt_page, pm_node, spare, FALSE);
    }

    if (ret == STATUS_SUCCESS) {
      meta_data[pmt_page] = i;//簇号

      PM_NODE_SET_BLOCKPAGE(root_table.page_mapping_nodes[i], pmt_block, pmt_page);

      /* last page is reserved for meta data */
      if (pmt_page < PAGE_PER_PHY_BLOCK - 1) {
        pmt_page++;
      }

      if (pmt_page == PAGE_PER_PHY_BLOCK - 1) {
        ret = UBI_Write(pmt_block, pmt_page, meta_data, NULL, FALSE);//PMT区块的最后一个存前63页映射的簇号
        if (ret == STATUS_SUCCESS) {
          block_dirty_table[pmt_block] = 0;
          pmt_page = 0;
          pmt_block++;
        }
      }
    }
  }

  if (ret == STATUS_SUCCESS) {
    /* set journal blocks */
    PM_NODE_SET_BLOCKPAGE(root_table.pmt_current_block, pmt_block, pmt_page);
    PM_NODE_SET_BLOCKPAGE(root_table.pmt_reclaim_block, pmt_block + 1, 0);

    /* update block dirty table */
    block_dirty_table[pmt_block] = 0;
    block_dirty_table[pmt_block + 1] = 0;//表示已分配
  }

  return ret;
}

STATUS PMT_Init() {
  UINT32 i;
  STATUS ret = STATUS_SUCCESS;
  
  
  
  /* init cache */
  for (i = 0; i < PMT_CACHE_COUNT; i++) {//4
    memset(pm_node_caches[i], 0, MPP_SIZE);
    pm_cache_origin_location[i] = INVALID_PM_NODE;
    pm_cache_cluster[i] = INVALID_CLUSTER;
  }

  /* PLR: the PMT is only validated after writing ROOT. do some test. */
  return ret;
}

STATUS PMT_Update(PGADDR page_addr, LOG_BLOCK block, PAGE_OFF page) {
  //uart_printf("PMT UPDATE\n");
  PMT_CLUSTER cluster = CLUSTER_INDEX(page_addr);//计算逻辑I地址所在簇号
  PM_NODE_ADDR* cluster_addr;
  LOG_BLOCK edit_block;
  STATUS ret = STATUS_SUCCESS;

  if (PM_NODE_IS_CACHED(root_table.page_mapping_nodes[cluster]) == FALSE) {
    /* load page in cache before updating bdt/hdi/root,
     * because it may cause a commit. */
    ret = PMT_Load(PM_NODE_BLOCK(root_table.page_mapping_nodes[cluster]),
                   PM_NODE_PAGE(root_table.page_mapping_nodes[cluster]),
                   cluster);
  }

  if (ret == STATUS_SUCCESS) {
    cluster_addr = PM_NODE_ADDRESS(root_table.page_mapping_nodes[cluster]);
    if (cluster_addr[PAGE_IN_CLUSTER(page_addr)] != INVALID_PM_NODE) {
      /* update BDT: increase dirty page count of the edited data block */
      edit_block = PM_NODE_BLOCK(cluster_addr[PAGE_IN_CLUSTER(page_addr)]);//目标映射所在簇在PMT区的块号
      block_dirty_table[edit_block]++;
      
      if(block_dirty_table[edit_block] == 63) {
          invalid_time[edit_block] = current_time;
      }
      
      ASSERT(block_dirty_table[edit_block] <= MAX_DIRTY_PAGES);
    }

    /* update PMT */
    if (block != INVALID_BLOCK) {
      ASSERT(page != INVALID_PAGE);
      PM_NODE_SET_BLOCKPAGE(cluster_addr[PAGE_IN_CLUSTER(page_addr)], block, page);//逻辑I的映射更新到新的逻辑II块页上
                                                                                   //相当于修改了SRAM中对应的pm_node_caches[][]
    } else {
      /* trim page, set it invalid page in PMT, and it will be
       * discarded in the next reclaim.
       */
      ASSERT(page == INVALID_PAGE);
      cluster_addr[PAGE_IN_CLUSTER(page_addr)] = INVALID_PM_NODE;
    }

    /* set dirty bit */
    PM_NODE_SET_DIRTY(root_table.page_mapping_nodes[cluster]);//将该簇映射设为脏，即被更新过了
  }

  return ret;
}

STATUS PMT_Search(PGADDR page_addr, LOG_BLOCK* block, PAGE_OFF* page) {
  PMT_CLUSTER cluster = CLUSTER_INDEX(page_addr);
  PM_NODE_ADDR* cluster_addr;
  PM_NODE_ADDR pm_node;
  STATUS ret = STATUS_SUCCESS;
  
  // JD
  //uart_printf("addr: %d\n", page_addr);
  if(ftl_read_state == 0) {
    if (PM_NODE_IS_CACHED(root_table.page_mapping_nodes[cluster]) == FALSE) {
      /* load page in cache */
      ret = PMT_Load(PM_NODE_BLOCK(root_table.page_mapping_nodes[cluster]),
                   PM_NODE_PAGE(root_table.page_mapping_nodes[cluster]),
                   cluster);
    }

    if (ret == STATUS_SUCCESS) {
      ASSERT(root_table.page_mapping_nodes[cluster] != INVALID_PM_NODE);

      cluster_addr = PM_NODE_ADDRESS(root_table.page_mapping_nodes[cluster]);
      ASSERT(cluster_addr != 0);

      pm_node = cluster_addr[PAGE_IN_CLUSTER(page_addr)];
      if (pm_node != INVALID_PM_NODE) {
        *block = PM_NODE_BLOCK(pm_node);
        *page = PM_NODE_PAGE(pm_node);
      } else {
        *block = INVALID_BLOCK;
        *page = INVALID_PAGE;
      }
    }
  }
  else if(ftl_read_state == 1) {
    UINT32 pmNodes[PM_PER_NODE];
    LOG_BLOCK pmBlock = PMTRESORE_START_BLOCK + (int)(cluster / PAGE_PER_PHY_BLOCK); // The logical block number containing the PM
    PAGE_OFF pmPage = cluster % PAGE_PER_PHY_BLOCK; // the page offset in pm block
    

    
    ftl_read_state = 0;
    ret = UBI_Read(pmBlock, pmPage, &(pmNodes[0]), NULL);
    ftl_read_state = 1;
    
    pm_node = pmNodes[PAGE_IN_CLUSTER(page_addr)];
    if (pm_node != INVALID_PM_NODE) {
      *block = PM_NODE_BLOCK(pm_node);
      *page = PM_NODE_PAGE(pm_node);
    } else {
      *block = INVALID_BLOCK;
      *page = INVALID_PAGE;
    };
    return ret;
  }
  // end JD

  return ret;
}

static STATUS PMT_Load(LOG_BLOCK block, PAGE_OFF page, PMT_CLUSTER cluster) {
  UINT32 i;
  PM_NODE_ADDR* cache_addr = NULL;
  STATUS ret = STATUS_SUCCESS;

  /* find the first empty cache slot */
  for (i = 0; i < PMT_CACHE_COUNT; i++) {
    if (pm_cache_origin_location[i] == INVALID_PM_NODE) {
      break;
    }
  }

  if (i == PMT_CACHE_COUNT) {
    i = 0;

    /* cache is full, commit to nand, and release all cache */
    ret = DATA_Commit();
    if (ret == STATUS_SUCCESS) {
      /* use updated PMT block and page */
      block = PM_NODE_BLOCK(root_table.page_mapping_nodes[cluster]);
      page = PM_NODE_PAGE(root_table.page_mapping_nodes[cluster]);
    }
  }

  /* read out the PM node from UBI */
  if (ret == STATUS_SUCCESS) {
    cache_addr = &((pm_node_caches[i])[0]);
    ret = UBI_Read(block, page, cache_addr, NULL);
  }

  /* update cache info */
  if (ret == STATUS_SUCCESS) {
    PM_NODE_SET_BLOCKPAGE(pm_cache_origin_location[i], block, page);

    /* update the cache address in memory to PMT table */
    root_table.page_mapping_nodes[cluster] = (UINT32) (cache_addr);
    

    /* the page mapping should be clean in ram */
    ASSERT((((UINT32 )(cache_addr)) & 0x3) == 0);

    pm_cache_cluster[i] = cluster;
  }

  return ret;
}

/* write back dirty node to UBI, and clear all cache */
STATUS PMT_Commit() {
  UINT32 i;
  PM_NODE_ADDR pm_node;
  STATUS ret = STATUS_SUCCESS;

  /* find the dirty cache nodes */
  for (i = 0; i < PMT_CACHE_COUNT; i++) {
    if (pm_cache_cluster[i] == INVALID_CLUSTER) {
      continue;
    }

    pm_node = root_table.page_mapping_nodes[pm_cache_cluster[i]];//根据簇号，得到SRAM地址
    ASSERT(PM_NODE_IS_CACHED(pm_node) == TRUE);
    if (PM_NODE_IS_DIRTY(pm_node) == FALSE) {//clean
      /* update pmt in root table */
      root_table.page_mapping_nodes[pm_cache_cluster[i]] =
          pm_cache_origin_location[i];
      continue;
    }
    //dirty
    /* check empty page space */
    if (PMT_CURRENT_PAGE != PAGE_PER_PHY_BLOCK) {
      /* last page is reserved */
      ASSERT(PMT_CURRENT_PAGE != (PAGE_PER_PHY_BLOCK - 1));

      if (ret == STATUS_SUCCESS) {
        /* write page to UBI */
        ret = UBI_Write(PMT_CURRENT_BLOCK, PMT_CURRENT_PAGE,
                        pm_node_caches[i], &pm_cache_cluster[i], FALSE);
        if (ret == STATUS_SUCCESS) {
          meta_data[PMT_CURRENT_PAGE] = pm_cache_cluster[i];
        }
      }

      if (ret == STATUS_SUCCESS) {
        PMT_CLUSTER pm_cluster = pm_cache_cluster[i];
        LOG_BLOCK old_pm_block;

        /* update pmt in root table */
        PM_NODE_SET_BLOCKPAGE(root_table.page_mapping_nodes[pm_cluster],
                              PMT_CURRENT_BLOCK, PMT_CURRENT_PAGE);

        /* update pmt journal */
        PM_NODE_SET_BLOCKPAGE(root_table.pmt_current_block, PMT_CURRENT_BLOCK,
                              PMT_CURRENT_PAGE+1);

        /* update the block dirty table */
        old_pm_block = PM_NODE_BLOCK(pm_cache_origin_location[i]);

        block_dirty_table[old_pm_block]++;

        ASSERT(block_dirty_table[old_pm_block] <= MAX_DIRTY_PAGES);
      }
    }

    if (PMT_CURRENT_PAGE == PAGE_PER_PHY_BLOCK - 1) {
      if (ret == STATUS_SUCCESS) {
        ret = UBI_Write(PMT_CURRENT_BLOCK, PMT_CURRENT_PAGE,
                        meta_data, NULL, FALSE);
      }

      if (ret == STATUS_SUCCESS) {
        /* flush WIP data on all dice */
        ret = UBI_Flush();
      }

      if (ret == STATUS_SUCCESS) {
        ret = pmt_reclaim_blocks();
      }
    }
  }

  if (ret == STATUS_SUCCESS) {
    /* init the PMT to clear all cache */
    ret = PMT_Init();
  }

  return ret;
}


//jsjpenn
//为了能在ftl_api.c中被调用

STATUS pmt_reclaim_blocks() {
  uart_printf("PMT reclaim??\n");
  UINT32 i = 0;
  UINT32 found_block = 0;
  UINT32 total_valid_page = 0;
  PAGE_OFF next_dirty_count = 0;
  PAGE_OFF target_dirty_count = MAX_DIRTY_PAGES;//63
  STATUS ret = STATUS_SUCCESS;

  /* find dirtiest block in different dice as new journal blocks */
  while (found_block != 1) {
    for (i = PMT_START_BLOCK; i < PMT_START_BLOCK + PMT_BLOCK_COUNT; i++) {
      //JD
      if (block_dirty_table[i] == target_dirty_count) {//63
      // end JD
        /* try to erase it */
        ret = UBI_ReadStatus(i);
      } else {
        /* set the next target dirty count */
        if (block_dirty_table[i] < target_dirty_count
            && block_dirty_table[i] > next_dirty_count) {
          next_dirty_count = block_dirty_table[i];
        }
        continue;
      }

      if (ret == STATUS_SUCCESS) {
        /* find a dirtiest block */
        total_valid_page = (MAX_DIRTY_PAGES - block_dirty_table[i]);
        found_block = 1;
        break;
      }
    }
    target_dirty_count = next_dirty_count;
  }

  if (ret == STATUS_SUCCESS) {
    if (total_valid_page != 0) {
      /* copy valid pages to the reclaim block */
      LOG_BLOCK reclaim_block;
      LOG_BLOCK dirty_block;
      PAGE_OFF reclaim_page = 0;
      PAGE_OFF page;

      reclaim_block = PM_NODE_BLOCK(root_table.pmt_reclaim_block);
      dirty_block = i;

      ret = UBI_Read(dirty_block, PAGE_PER_PHY_BLOCK - 1, clusters, NULL);
      if (ret == STATUS_SUCCESS) {
        for (page = 0; page < PAGE_PER_PHY_BLOCK - 1; page++) {
          PMT_CLUSTER cluster = clusters[page];
          PM_NODE_ADDR pm_node = root_table.page_mapping_nodes[cluster];
          UINT32 cleared_cache_index = INVALID_INDEX;

          /* if cached, just need to copy clean page */
          if (PM_NODE_IS_CACHED(pm_node) == TRUE) {
            if (PM_NODE_IS_DIRTY(pm_node) == TRUE) {
              /* dirty page will be re-written by commit */
              pm_node = INVALID_PM_NODE;
            } else {
              /* reclaim clean cached pages */
              UINT32 i;

              for (i = 0; i < PMT_CACHE_COUNT; i++) {
                if (pm_cache_cluster[i] == cluster) {
                  break;
                }
              }

              ASSERT(i != PMT_CACHE_COUNT);
              pm_node = pm_cache_origin_location[i];
              cleared_cache_index = i;
            }
          }

          if (pm_node != INVALID_PM_NODE &&
          PM_NODE_BLOCK(pm_node) == dirty_block &&
          PM_NODE_PAGE(pm_node) == page) {
            /* copy valid page to reclaim block */
            ret = UBI_Read(dirty_block, page, pm_node_buffer, NULL);
            if (ret == STATUS_SUCCESS) {
              ret = UBI_Write(reclaim_block, reclaim_page, pm_node_buffer, NULL,
              FALSE);
            }

            if (ret == STATUS_SUCCESS) {
              /* update mapping */
              PM_NODE_SET_BLOCKPAGE(root_table.page_mapping_nodes[cluster],
                                    reclaim_block, reclaim_page);
              meta_data[reclaim_page] = cluster;
              reclaim_page++;

              /* clear it from cache */
              if (cleared_cache_index != INVALID_INDEX) {
                memset(pm_node_caches[cleared_cache_index], 0, MPP_SIZE);
                pm_cache_origin_location[cleared_cache_index] =
                INVALID_PM_NODE;
                pm_cache_cluster[cleared_cache_index] = INVALID_CLUSTER;
              }
            }
          }
        }
      }

      /* erase dirty block, and then update journals */
      if (ret == STATUS_SUCCESS) {
        ret = UBI_Erase(dirty_block, dirty_block);
      }

      if (ret == STATUS_SUCCESS) {
        PM_NODE_SET_BLOCKPAGE(root_table.pmt_current_block, reclaim_block,
                              reclaim_page);
        PM_NODE_SET_BLOCKPAGE(root_table.pmt_reclaim_block, dirty_block, 0);

        /* reset the BDT */
        block_dirty_table[reclaim_block] = 0;
        block_dirty_table[dirty_block] = 0;
      }
    } else {
      if (ret == STATUS_SUCCESS) {
        /* the die is NOT busy */
        ret = UBI_Erase(i, i);
      }

      if (ret == STATUS_SUCCESS) {
        PM_NODE_SET_BLOCKPAGE(root_table.pmt_current_block, i, 0);

        /* reset the BDT */
        block_dirty_table[i] = 0;
      }
    }
  }

  return ret;
}