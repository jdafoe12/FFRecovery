/*************************************************************************
*
*   Used with ICCARM and AARM.
*
*    (c) Copyright IAR Systems 2009
*
*    File name   : main.c
*    Description :
*
*
*COMPATIBILITY
*=============
*
*   The USB Mass storage example project is compatible with Embedded Artsists
*  LPC313x evaluation board. By default the project is configured to use the
*  J-Link JTAG interface.
*
*CONFIGURATION
*=============
*
*  The Project contains the following configurations:
*
*  Debug: run in iRAM
*
*
*    History :
*    1. Date        : 22.8.2009
*       Author      : Stanimir Bonev
*       Description : initial revision.
*
*    $Revision: 32285 $
**************************************************************************/

/** include files **/
#include "includes.h"

#include <NXP/iolpc3131.h>
#include <stdio.h>
#include <string.h>
#include "arm926ej_cp15_drv.h"
#include "arm_comm.h"
#include "drv_spi.h"
#include "drv_spinor.h"
#include "drv_intc.h"
#include "math.h"

#include "lpc313x_timer_driver.h"
#include "lpc313x_usbotg.h"
#include "lpc313x_usbd_driver.h"
#include "lpc313x_chip.h"
#include "mscuser.h"
#include "usbcore.h"
#include "usbhw.h"

#include <onfm.h>
#include <core\inc\cmn.h>
#include <core\inc\buf.h>
#include <core\inc\mtd.h>
//#include <core\polar\include\polarssl\aes.h>

//jsjpenn
#include  <core\inc\ubi.h>//在main.c中输出调试信息

#define SDRAM_BASE_ADDR 0x30000000
#define SDRAM_SIZE      0x02000000

/** external functions **/
extern void InitSDRAM(void);

/** internal functions **/
extern void USB_EndPoint0 (UNS_32 event);


#pragma data_alignment=DMA_BURST_BYTES
unsigned char sector_buffer[SECTOR_SIZE];

#pragma data_alignment=DMA_BURST_BYTES
unsigned char read_sector_buffer[SECTOR_SIZE];

#pragma data_alignment=DMA_BURST_BYTES
UINT8 write_page_buffer[MPP_SIZE];
#pragma data_alignment=DMA_BURST_BYTES
UINT8 read_page_buffer[MPP_SIZE];

#define ISROM_MMU_TTBL              (0x1201C000)
#define USER_SPACE_SECTOR_COUNT     (ONFM_Capacity())


//jsjpenn
int g_state = 1;
int g_ReceiveCommandNum = 0;
UINT32 g_IdleCcount=0;

//JD
int ftl_read_state = 0;



/***********************************************************************
*
* Function: USB_Reset_Event
*
* Purpose: USB Reset Event Callback
*
* Processing:
*     Called automatically on USB Reset Event.
*
* Parameters: None
*
* Outputs: None
*
* Returns: Nothing
*
* Notes: None
*
***********************************************************************/
void USB_Reset_Event(void)
{
  USB_ResetCore();
}

#if USB_CONFIGURE_EVENT
/***********************************************************************
*
* Function: USB_Configure_Event
*
* Purpose: USB Configure Event Callback
*
* Processing:
*     Called automatically on USB configure Event.
*
* Parameters: None
*
* Outputs: None
*
* Returns: Nothing
*
* Notes: None
*
***********************************************************************/
void USB_Configure_Event (void)
{
  
}
#endif

/***********************************************************************
*
* Function: USB_EndPoint1
*
* Purpose: USB Endpoint 1 Event Callback
*
* Processing:
*     Called automatically on USB Endpoint 1 Event
*
* Parameters: None
*
* Outputs: None
*
* Returns: Nothing
*
* Notes: None
*
***********************************************************************/
void USB_EndPoint1 (UNS_32 event)
{
  switch (event)
  {
  case USB_EVT_OUT_NAK:
    MSC_BulkOutNak();
    break;
  case USB_EVT_OUT:
    MSC_BulkOut();
    break;
  case USB_EVT_IN_NAK:
    MSC_BulkInNak();
    break;
  case USB_EVT_IN:
    MSC_BulkIn();
    break;
  }
}


static void init_usb()
{
  LPC_USBDRV_INIT_T usb_cb;
  
  // Enable USB interrupts
  // Install Interrupt Service Routine, Priority
  INTC_IRQInstall(USB_ISR, IRQ_USB, USB_INTR_PRIORITY,0);
  
  /* initilize call back structures */
  memset((void*)&usb_cb, 0, sizeof(LPC_USBDRV_INIT_T));
  usb_cb.USB_Reset_Event = USB_Reset_Event;
  usb_cb.USB_P_EP[0] = USB_EndPoint0;
  usb_cb.USB_P_EP[1] = USB_EndPoint1;
  usb_cb.ep0_maxp = USB_MAX_PACKET0;
  /* USB Initialization */
  USB_Init(&usb_cb);
}

//jsjpenn
static void usb_user_task_loop()
{  
  //jsjpenn
  int ret=0;
  int icount=0;  
  
  int pop;   
  while (1)
  {
    pop = ut_pop;
    //push = ut_push;
    //if (ut_pop != ut_push)
    if (pop != ut_push)
    {
      if (ut_list[pop].type == UT_WRITE)
      {
        LED_SET(LED2);         
       
        //jsjpenn
        ret=ONFM_Write(ut_list[pop].offset,
                   ut_list[pop].length,
                   ut_list[pop].buffer);         
        
        
        LED_CLR(LED2);        
      }
      else if (ut_list[pop].type == UT_READ)
      {
        if (Read_BulkLen == 0)
        {
          LED_SET(LED1);

          ONFM_Read(ut_list[pop].offset,
                    ut_list[pop].length,
                    ut_list[pop].buffer);
          
          LED_CLR(LED1);
          
          /* tell the IN NAK INT the buffer is ready to prime */
          Read_BulkLen = (ut_list[pop].length)*MSC_BlockSize;
        }
      }
      else
      {
        ASSERT(ut_list[pop].type == UT_MERGE);
        
        if (merge_stage == MERGE_START)
        {
          ONFM_Read(ut_list[pop].offset,
                    ut_list[pop].length,
                    ut_list[pop].buffer);
          
          merge_stage = MERGE_FINISH;
        }
      }
      
      /* next write operation */
      ut_pop = (ut_pop+1)%UT_LIST_SIZE;
    }
    
    //jsjpenn
    else{      
//      //系统空闲时间增加 
//      g_IdleCcount++;
//      icount++;
//      if(icount>200000){
//        //uart_printf("N=%d",g_IdleCcount/200000); //输出调试时间信息
//        icount=0;
//      }
//      
//      if(g_state==1){
//        //测试ransom在桌面没有文件-加密5M文件时用-100000000
//        //平台恢复-写入要lock的300MB文件用160000000
//        if(g_IdleCcount>20000000){          
//          ret=Start_change_state();          
//          if(ret==STATUS_Remount){
//            break;//跳出while死循环          
//          }        
//        }
//      }
//      
//      if(g_state==2){
//        //测试ransom在桌面没有文件-加密5M文件时用-500000000
//        //平台恢复-恢复写入的300MB文件用（再写5M数据）-40000000
//        if(g_IdleCcount>20000000){          
//          ret=Start_change_state();                   
//          if(ret==STATUS_Remount){
//            break;//跳出while死循环          
//          }        
//        }
//      }
    }
    //jsjpennend   
    
  }
}

static void SDRAM_Test(void)
{
  char s[64];
  sprintf(s,"\n\rStart SDRAM Test\n\r");  
  UartWrite((unsigned char *)s,strlen(s));

  /*32bit access test*/
  sprintf(s,"32-bits write\n\r");
  UartWrite((unsigned char *)s,strlen(s));
  /*Start from stram base address*/
  volatile unsigned int * uint_dest = (unsigned int *)SDRAM_BASE_ADDR;
  for(int i = 0; i < SDRAM_SIZE/sizeof(int); i++,uint_dest++)
  {
    /*32-bits write*/
    * uint_dest = i;
  }
  /*32-bits verify*/
  sprintf(s,"32-bits verify\n\r");
  UartWrite((unsigned char *)s,strlen(s));

  uint_dest = (unsigned int *)SDRAM_BASE_ADDR;
  for(int i = 0; i < SDRAM_SIZE/sizeof(int); i++,uint_dest++)
  {
    /*32-bits read*/
    if (* uint_dest != i)
    {
      /*verify error*/
      sprintf(s,"SDRAM 32-bits R/W Error at address %0x\n\r",(unsigned int)uint_dest);
      UartWrite((unsigned char *)s,strlen(s));
      break;
    }
  }
  
  /*16-bits access test*/
  sprintf(s,"16-bits write\n\r");
  UartWrite((unsigned char *)s,strlen(s));
  /*Start from stram base address*/
  volatile unsigned short * ushrt_dest = (unsigned short *)SDRAM_BASE_ADDR;
  for(int i = 0; i < SDRAM_SIZE/sizeof(short); i++,ushrt_dest++)
  {
    /*16-bits write*/
    *ushrt_dest = (i^(i>>16));
  }
  /*16-bits verify*/
  sprintf(s,"16-bits verify\n\r");
  UartWrite((unsigned char *)s,strlen(s));

  ushrt_dest = (unsigned short *)SDRAM_BASE_ADDR;
  for(int i = 0; i < SDRAM_SIZE/sizeof(short); i++,ushrt_dest++)
  {
    /*16-bits read*/
    if ( *ushrt_dest != ((i^(i>>16))&0xFFFF))
    {
      /*verify error*/
      sprintf(s,"SDRAM 16-bits R/W Error at address 0x%0x\n\r",(unsigned int)ushrt_dest);
      UartWrite((unsigned char *)s,strlen(s));
      break;
    }
  }
  
  /*8-bits access test*/
  sprintf(s,"8-bits write\n\r");
  UartWrite((unsigned char *)s,strlen(s));
  /*Start from stram base address*/
  volatile unsigned char * uchar_dest = (unsigned char *)SDRAM_BASE_ADDR;
  for(int i = 0; i < SDRAM_SIZE; i++,uchar_dest++)
  {
    /*16-bits write*/
    *uchar_dest = i^(i>>8);
  }
  /*8-bits verify*/
  sprintf(s,"8-bits verify\n\r");
  UartWrite((unsigned char *)s,strlen(s));

  uchar_dest = (unsigned char *)SDRAM_BASE_ADDR;
  for(int i = 0; i < SDRAM_SIZE; i++,uchar_dest++)
  {
    /*8-bits read*/
    if ( *uchar_dest != ((i^(i>>8))&0xFF))
    {
      /*verify error*/
      sprintf(s,"SDRAM 8-bits R/W Error at address %0x\n\r",(unsigned int)ushrt_dest);
      UartWrite((unsigned char *)s,strlen(s));
      break;
    }
  }

  sprintf(s,"SDRAM Test end\n\r");  
  UartWrite((unsigned char *)s,strlen(s));

}

/*************************************************************************
* Function Name: main
* Parameters: None
*
* Return: None
*
* Description: Getting Started main loop
*
*************************************************************************/
void main(void)
{
  //jsjpenn    
loop: 
  int onfm_ret = 0;
#if 0 //注释掉调试用的代码 #if 1就是将注释去掉
  CP15_Mmu(FALSE);            // Disable MMU
  CP15_ICache(TRUE);          // Enable ICache

  CP15_SysProt(FALSE);
  CP15_RomProt(TRUE);
  CP15_SetTtb((Int32U *)ISROM_MMU_TTBL);  //Set translation table base address
  CP15_SetDomain( (DomainManager << 2*1) | (DomainClient << 0)); // Set domains
  CP15_Mmu(TRUE);             // Enable MMU
  CP15_Cache(TRUE);           // Enable ICache,DCache
#endif  
  
#ifndef BOOT_LEVEL_2
  InitClock();
  InitSDRAM();
  InitSPINOR();
#endif

  // Uncomment for SDRAM experiment.
   InitSDRAM();

  /* TODO:
  * - test DMA copy in SDRAM w/ and w/o clock enable.
  * - test USB RAMDisk speed
  * - test mtd speed
  * - test ONFM-USB
  * - debug, use K9HAG.
  */
  
  /*Init Interrupt Controller.
  Arm Vector Copy to beginnint of the IRAM*/
  INTC_Init((Int32U *)ISRAM_ESRAM0_BASE);
  /*Remap IRAM at address 0*/
  SYSCREG_ARM926_901616_LP_SHADOW_POINT = ISRAM_ESRAM0_BASE;
  
  init_usb();
  
 
  
  //jsj修改
  //原来是只执行ONFM_Mount()，若成功就不执行ONFM_Format()了，使得不必每次下载完代码都需要格式化文件系统
  //现在改成先执行ONFM_Format()，再执行ONFM_Mount()，这样每次下载完代码都需要格式化文件系统
  
  // init ONFM
  //onfm_ret = ONFM_Mount();
  
  
  //jsjpenn
  if(g_state== 1)
  {
    onfm_ret = -1;
  }else{
    onfm_ret = ONFM_Mount();
  }
  
  if (onfm_ret != 0) {
    /* init failed, try to format */
    onfm_ret = ONFM_Format();
    if (onfm_ret == 0) {
      onfm_ret = ONFM_Mount();
    }
  }
  
  
  if (onfm_ret == 0) {
    MSC_Init();  //jsjpenn会调用FTL_Capacity()  
   
    INTC_IntEnable(IRQ_USB, 1);
    __enable_irq();    
    /* wait */
    timer_wait_ms(NULL, 10);     
    
    /* USB Connect */
    USB_Connect(TRUE);
  }  
  
  if(g_state== 1)
  {
    uart_printf("%s: State 1 is ready, start to fotmat file system\r\n",__func__);
  }else{
    uart_printf("%s: State 2 is ready, we can start to test malware\r\n",__func__);
  }  
  
  //jsj 运行到此时提示格式化文件系统 
  
  /* main loop to handle usb read/write tasks in USER SPACE */
  usb_user_task_loop();   
 
      
  /* TODO: call unmount to flush and check program status
   periodly after a long time delay. Avoid PLR or unsafe plug-out  */
  ONFM_Unmount();
  
  //jsjpenn
  uart_printf("%s: Unmount done \r\n",__func__); 
  
  goto loop; 
 
  /* TODO: use watchdog timer, to reset system */
}
