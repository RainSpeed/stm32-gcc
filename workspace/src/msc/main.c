/******************** (C) COPYRIGHT 2011 STMicroelectronics ********************
* File Name					: main.c
* Author						 : MCD Application Team
* Version						: V3.3.0
* Date							 : 21-March-2011
* Description				: Mass Storage demo main file
********************************************************************************
* THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
* WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE TIME.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#ifdef STM32L1XX_MD
 #include "stm32l1xx.h"
#else
 #include "stm32f10x.h"
#endif /* STM32L1XX_MD */
 
#include "usb_lib.h"
#include "hw_config.h"
#include "usb_pwr.h"
#include "user_uart.h"
#include "flash.h"
#include <stdio.h>
#define MAIN_DEBUG 1
extern uint16_t MAL_Init (uint8_t lun);

void gpio_setup(void);

GPIO_InitTypeDef GPIO_InitStructure;

/* ���ҵ�һ��֪��Ҫ������ŵ���ߵ�ʱ����ʵ���ǣ��Ǿܾ��� */
uint32_t Yamaha_Counter = 0;
uint32_t clock = 0;
uint8_t isYamaha = 0;

void gpio_setup(void)
{
			//ʹ�ܻ���ʧ�� APB2 ����ʱ�� //��������IOʱ�� RCC_APB2Periph_AFIO
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 ;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Init(GPIOA, &GPIO_InitStructure); 
 
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 ;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Init(GPIOB, &GPIO_InitStructure); 
}

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
void SysTick_Handler(void)
{//Ħ��Ħ��
	clock++;
	if((clock % 10) == 0)
	{
		dbg("Hello, Clock = %d, Yamaha Count = %d\r\n",(int) clock, (int)Yamaha_Counter);
	}
	if((clock == 40) && (Yamaha_Counter >= 50) && (!isYamaha))
	{
		isYamaha = 1;
		GPIO_ResetBits(GPIOA, GPIO_Pin_8); // USB disconnect
		dbg("Detected Yamaha PSR-S500 series,Disconnect USB\r\n");
	}
	if((clock == 60) && isYamaha)
	{
		GPIO_SetBits(GPIOA, GPIO_Pin_8);
		dbg("Now repluggins the USB\r\n");
	}
}
/* Extern variables ----------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/
/*******************************************************************************
* Function Name	: main.
* Description		: Main routine.
* Input					: None.
* Output				 : None.
* Return				 : None.
*******************************************************************************/
int main(void)
{
	gpio_setup();
	UART1_GPIO_Configuration();
	UART1_Configuration();
#ifdef	MAIN_DEBUG 
	dbg("System Start\r\n");
#endif
	ramdisk_init();
	Set_System();//����USB���ƽ�
	Set_USBClock();//����usbʱ��
	Led_Config();
	GPIO_ResetBits(GPIOA, GPIO_Pin_8);
	GPIO_SetBits(GPIOB, GPIO_Pin_12);
	USB_Interrupts_Config();//����USB���ȼ�
	USB_Init();//��ʼ��usb ������Դ���ж�ʹ��,bDeviceState=UNCONNECT
	#ifdef	MAIN_DEBUG 
			dbg("wait host configured\r\n");
	#endif
	GPIO_SetBits(GPIOA, GPIO_Pin_8);
	while (bDeviceState != CONFIGURED);//�ȴ�??�ж������ô˱�־??
	SysTick_Config(9600000); /** һ����Σ���ħ��Ĳ��� **/

	while (1)
	{
	}

}

#ifdef USE_FULL_ASSERT
/*******************************************************************************
* Function Name	: assert_failed
* Description		: Reports the name of the source file and the source line number
*									where the assert_param error has occurred.
* Input					: - file: pointer to the source file name
*									- line: assert_param error line source number
* Output				 : None
* Return				 : None
*******************************************************************************/
void assert_failed(uint8_t* file, uint32_t line)
{
	/* User can add his own implementation to report the file name and line number,
		 ex: dbg("Wrong parameters value: file %s on line %d\r\n", file, line) */

	/* Infinite loop */
	while (1)
	{}
}
#endif

/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
