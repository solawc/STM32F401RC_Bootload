/*
 HID_Bootloader.c

 Copyright (c) 2021-2022 sola

 SimpleBootloader is an open source bootloader. It follows the open 
 source protocol of GPL 3.0, and users can republish it based on the 
 GPL 3.0 protocol.
*/

/*
  Note:
  HID bootloader is originally an open source software, which can 
  be updated through USB/Serial. It originally adapted STM32F1XX and STM32F4XX, 
  and compiled using GCC+Makefile. The actual application is mainly to be 
  compatible with Arduino protocol, so that STM32F1XX and STM32F4XX can burn 
  firmware and update through Arduino.
  Info:
  +-----------------------------------------------------------------------+
  |         HID-Flash v2.2.1 - STM32 HID Bootloader Flash Tool            |
  |     (c)      2018 - Bruno Freitas       http://www.brunofreitas.com   |
  |     (c) 2018-2019 - Vassilis Serasidis  https://www.serasidis.gr      |
  |   Customized for STM32duino ecosystem   https://www.stm32duino.com    |
  +-----------------------------------------------------------------------+
*/

#include "HID_Bootloader.h"

#define HID_RX_SIZE         64
#define USER_CODE_OFFSET    0x8000
#define SECTOR_SIZE         1024

uint32_t magic_val;
uint16_t erase_page = 1;



volatile uint8_t new_data_is_received = 0;  // 用来标志是否有数据过来
volatile uint8_t wait_ack_flag = 0;         // 用于等待有应答指令返回       
volatile uint32_t hidTick = 0;      

static uint8_t CMD_SIGNATURE[7] = {'B','T','L','D','C','M','D'};
static uint8_t CMD_DATA_RECEIVED[8] = {'B','T','L','D','C','M','D',0x02};

// static uint8_t CMD_RESET_PAGES[8] = {'B','T','L','D','C','M','D', 0X00};
static uint8_t CMD_RESET_PAGES[8] = {'B','L','T','T','T','T','T','T'};

static uint8_t CMD_OK_ACK[2] = {'O', 'K'};

uint8_t USB_RX_Buffer[HID_RX_SIZE];

static uint8_t pageData[SECTOR_SIZE];

HID_RX_t bootRX;
uint8_t isHaveCMD = 0;

void write_flash_sector(uint32_t currentPage);
void print_HID_Info(void);
uint8_t compareCMD(void);

void HID_Bootloader_Task(void) {

    /* Init bootloader GPIO. if you need. */
    boot_GPIO_Init();  

    /* Reset UART RX buff. */
    memset(bootRX.hid_rx_buff, 0, sizeof(bootRX.hid_rx_buff));
    bootRX.hid_rx_len = 0;

    /* Enable UART RX HandleIRQ to receive data. */ 
    hal_uart_rx_irq_enable();       

    /* Get MCU restart data. */
    magic_val = bootGet_BAK_Register(); 

    /* wait 3s to connect bootloader */
    hidTick = 5000;
    while(hidTick) {
        isHaveCMD = compareCMD();
        if(isHaveCMD) break;
    }

    /* if no, jump to app now. */
    if(!isHaveCMD) {    
        typedef void (*pFunction)(void);
        pFunction Jump_To_Application;
        uint32_t JumpAddress;

        JumpAddress = *(__IO uint32_t*) (FLASH_BASE + USER_CODE_OFFSET + 4);
        Jump_To_Application = (pFunction) JumpAddress;
        __set_MSP(*(uint32_t *) (FLASH_BASE + USER_CODE_OFFSET));
        Jump_To_Application(); 
    }

    bootSet_EnableBkUpAccess();
    bootSet_BAK_Register();
    bootSet_DisableBkUpAccess();

    static volatile uint32_t current_Page = (USER_CODE_OFFSET / 1024);
    static volatile uint16_t currentPageOffset = 0;

    while(1) {
        if(new_data_is_received == 1) {     // 有64buff数据
            new_data_is_received = 0;
            
            // 查询buff内是否有指令
            if (memcmp(USB_RX_Buffer, CMD_SIGNATURE, sizeof (CMD_SIGNATURE)) == 0) {
                switch(USB_RX_Buffer[7]) {
                    
                    /*------------ Reset pages */
                    case 0x00: 
                        // bootSendReport(CMD_OK_ACK, 2); 
                        current_Page = 16;
                        currentPageOffset = 0;
                        erase_page = 1;
                    break;

                    case 0x01:
                        // bootSendReport(CMD_OK_ACK, 2);

                        /*------------- Reset MCU */
                        if (currentPageOffset > 0) {

                        /* There are incoming
                            data that are less
                            than sector size
                            (16384) */
                            write_flash_sector(current_Page);
                        }
                        HAL_Delay(100);
                        HAL_NVIC_SystemReset();
                    break;
                }
            }else {
                    memcpy(pageData + currentPageOffset, USB_RX_Buffer, HID_RX_SIZE);
                    currentPageOffset += HID_RX_SIZE;
                    if (currentPageOffset == SECTOR_SIZE) {
                    write_flash_sector(current_Page);
                    current_Page++;
                    currentPageOffset = 0;
                    CMD_DATA_RECEIVED[7] = 0x02;
                    // USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, CMD_DATA_RECEIVED, 8);
                    bootSendReport(CMD_DATA_RECEIVED, 8);   // 应答
                }
            }
        }
    }
}


/* Debug UART HandleIRQ */
void DEBUG_UART_IRQHANDLER(void) {

    volatile uint16_t data;

    if(hal_get_uart_rx_flag() == SET) {
        data = BspUartReadData();
        bootRX.hid_rx_buff[bootRX.hid_rx_len] = data;
        bootRX.hid_rx_len++;
        // if(bootRX.hid_rx_len == (64 - 1)) {
        if(bootRX.hid_rx_len == 64) {
            new_data_is_received = 1;
            memcpy(USB_RX_Buffer, bootRX.hid_rx_buff, sizeof(bootRX.hid_rx_buff));
            bootRX.hid_rx_len = 0;
        }
    }

    /* 进入串口空闲中断, 接收不定长数据 */
    // if((hal_get_uart_idle_flag() == SET) && (bootRX.hid_rx_len != 0)) {
    //     new_data_is_received = 1;
    //     bootRX.hid_rx_len = 0;
    //     memcpy(USB_RX_Buffer, bootRX.hid_rx_buff, sizeof(bootRX.hid_rx_buff));

    //     __HAL_UART_CLEAR_FLAG(&debug_uart, UART_FLAG_IDLE);
    // }

    HAL_UART_IRQHandler(&debug_uart);
}



void write_flash_sector(uint32_t currentPage) {
  uint32_t pageAddress = FLASH_BASE + (currentPage * SECTOR_SIZE);
  uint32_t SectorError;

//   HAL_GPIO_WritePin(LED_1_PORT, LED_1_PIN, GPIO_PIN_SET);	
  FLASH_EraseInitTypeDef EraseInit;
  HAL_FLASH_Unlock();
  
  /* Sector to the erase the flash memory (16, 32, 48 ... kbytes) */
  if ((currentPage == 16) || (currentPage == 32) ||
      (currentPage == 48) || (currentPage == 64) ||
      (currentPage % 128 == 0)) {
    EraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInit.VoltageRange  = FLASH_VOLTAGE_RANGE_3;

    /* Specify sector number. Starts from 0x08004000 */
    EraseInit.Sector = erase_page++;
                                              
    /* This is also important! */
    EraseInit.NbSectors = 1;
    HAL_FLASHEx_Erase(&EraseInit, &SectorError);
  }

  uint32_t dat;
  for (int i = 0; i < SECTOR_SIZE; i += 4) {
    dat = pageData[i+3];
    dat <<= 8;
    dat += pageData[i+2];
    dat <<= 8;
    dat += pageData[i+1];
    dat <<= 8;
    dat += pageData[i];
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, pageAddress + i, dat);
  }
//   HAL_GPIO_WritePin(LED_1_PORT, LED_1_PIN,GPIO_PIN_RESET);  
  HAL_FLASH_Lock();
}


void print_HID_Info(void) {

    printf("\n+-----------------------------------------------------------------------+\n");
    printf  ("|         HID-Flash v2.2.1 - STM32 HID Bootloader Flash Tool            |\n");
    printf  ("|     (c)      2018 - Bruno Freitas       http://www.brunofreitas.com   |\n");
    printf  ("|     (c) 2018-2019 - Vassilis Serasidis  https://www.serasidis.gr      |\n");
    printf  ("|   Customized for STM32duino ecosystem   https://www.stm32duino.com    |\n");
    printf  ("+-----------------------------------------------------------------------+\n\n");
}

uint8_t compareCMD(void) {
        if (memcmp(bootRX.hid_rx_buff, CMD_RESET_PAGES, sizeof (CMD_RESET_PAGES)) == 0) {
            __disable_irq();
            memset(bootRX.hid_rx_buff, 0, sizeof(bootRX.hid_rx_buff));
            bootRX.hid_rx_len = 0;
            __enable_irq();
            bootSendReport(CMD_OK_ACK, 2);
            return 1;
        }else{
            return 0;
        }
}