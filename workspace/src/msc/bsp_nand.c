#include "string.h"
#include "stdio.h"
#include "bsp_nand.h"
#include "user_uart.h"
/*
	���ڴ����뽨��������
	* һ��LBN->PBN��ӳ���LUT����СΪBLOCK���� * 2
	* һ�������BBT, ��СΪBLOCK����/8
	* ���߽�������ʱɨ��Spare��������
	* Ϊ�˷�ֹ���ʧ�ܵĲҾ緢������0����ΪBBT�Ͳ���ϵͳ��Ϣ�Ĵ洢��
	* д��ʱ���ж��Ƿ�����Ϊ�գ�Ϊ����д�룬�ǿ�����Flashβ��Ѱ�ҿ��п�����������д�롣
	* д����Ϻ����Դ�����������������������߼����ַ�����������������������߼����ַ����ѵ�ַд��0xffff
	* Ȼ������ڴ��е�LUT��д��ʱ��������������10�Σ���10�ξ�ʧ���򷵻ش���
	* �������������ڿ�����������������д����
*/

/* �߼����ӳ����ÿ�������2%���ڱ��������������ά������1024�� LUT = Look Up Table */
static uint16_t s_usLUT[NAND_BLOCK_COUNT]; 

static uint16_t s_usValidDataBlockCount;	/* ��Ч�����ݿ���� */

static uint8_t s_ucTempBuf[NAND_PAGE_TOTAL_SIZE];	/* �󻺳�����2112�ֽ�. ���ڶ����Ƚ� */

static uint8_t NAND_BuildLUT(void);
static uint16_t NAND_FindFreeBlock (void);
static uint8_t NAND_MarkUsedBlock(uint32_t _ulBlockNo);
static void NAND_MarkBadBlock(uint32_t _ulBlockNo);
static uint16_t NAND_AddrToPhyBlockNo(uint32_t _ulMemAddr);
static uint8_t NAND_IsBufOk(uint8_t *_pBuf, uint32_t _ulLen, uint8_t _ucValue);
uint8_t NAND_WriteToNewBlock(uint32_t _ulPhyPageNo, uint8_t *_pWriteBuf, uint16_t _usOffset, uint16_t _usSize);
static uint8_t NAND_IsFreeBlock(uint32_t _ulBlockNo);
static uint8_t NAND_IsBadBlock(uint32_t _ulBlockNo);

/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_Init
*	����˵��: ����FSMC��GPIO����NAND Flash�ӿڡ�������������ڶ�дnand flashǰ������һ�Ρ�
*	��    �Σ���
*	�� �� ֵ: ��
*********************************************************************************************************
*/
#define TIME_OUT_VALUE 1000000000UL
/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_Init
*	����˵��: ����FSMC��GPIO����NAND Flash�ӿڡ�������������ڶ�дnand flashǰ������һ�Ρ�
*	��    �Σ���
*	�� �� ֵ: ��
*********************************************************************************************************
*/
uint8_t	 SPI_Readbyte(uint8_t Data)
{		 
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
	SPI_I2S_SendData(SPI1, Data);			
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);	
	return SPI_I2S_ReceiveData(SPI1);	
}
void
NAND_WriteEnable(void)
{
	SPI_CS_ENABLE;
	SPI_Readbyte(WRITE_ENABLE);
	SPI_CS_DISABLE;
}

void
NAND_WriteDisable(void)
{
	SPI_CS_ENABLE;
	SPI_Readbyte(WRITE_DISABLE);
	SPI_CS_DISABLE;
}

uint8_t
NAND_GetFeatures(uint8_t Address)
{
	uint8_t fData = 0;
	SPI_CS_ENABLE;
	SPI_Readbyte(GET_FEATURES);
	SPI_Readbyte(Address);
	fData = SPI_Readbyte(DUMMY_BYTE);
	SPI_CS_DISABLE;
	return fData;
}

void
NAND_SetFeatures(uint8_t Address, uint8_t fData)
{
	NAND_WriteEnable();
	SPI_CS_ENABLE;
	SPI_Readbyte(SET_FEATURES);
	SPI_Readbyte(Address);
	SPI_Readbyte(fData);
	SPI_CS_DISABLE;
	NAND_WriteDisable();
}

static void FSMC_NAND_Init(void)
{
	dbg("nand_init()\r\n");
	SPI_CS_DISABLE;
	SPI_HOLD_DISABLE;
	SPI_WP_DISABLE;
	NAND_SetFeatures(FEATURE_REG, 0x10); //enable the internal ECC operation
	NAND_SetFeatures(PROTECTION_REG, 0x80); //disable all protection
	dbg("Protection Register:	0x%x\r\n", NAND_GetFeatures(PROTECTION_REG));
	dbg("Feature Register:	 0x%x\r\n", NAND_GetFeatures(FEATURE_REG));
	dbg("Status Register:		0x%x\r\n", NAND_GetFeatures(STATUS_REG));
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_ReadID
*	����˵��: ��NAND Flash��ID��ID�洢���β�ָ���Ľṹ������С�
*	��    �Σ���
*	�� �� ֵ: 32bit��NAND Flash ID
*********************************************************************************************************
*/
uint32_t NAND_ReadID(void)
{
	uint32_t data = 0;	
	SPI_CS_ENABLE;

	SPI_Readbyte(READ_ID);
	SPI_Readbyte(DUMMY_BYTE);
	data = (SPI_Readbyte(DUMMY_BYTE) << 24) + (SPI_Readbyte(DUMMY_BYTE) << 16)
		;//+ (SPI_Readbyte(DUMMY_BYTE) << 8);
	
	SPI_CS_DISABLE;
	
	return data;
}


/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_PageCopyBack
*	����˵��: ��һҳ���ݸ��Ƶ�����һ��ҳ��Դҳ��Ŀ��ҳ����ͬΪż��ҳ��ͬΪ����ҳ��
*	��    �Σ�- _ulSrcPageNo: Դҳ��
*             - _ulTarPageNo: Ŀ��ҳ��
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*
*	˵    ���������ֲ��Ƽ�����ҳ����֮ǰ����У��Դҳ��λУ�飬������ܻ����λ���󡣱�����δʵ�֡�
*
*********************************************************************************************************
*/
static uint8_t FSMC_NAND_PageCopyBack(uint32_t _ulSrcPageNo, uint32_t _ulTarPageNo)
{		
	uint8_t wrap;
	// Read it to cache
	SPI_CS_ENABLE;
	SPI_Readbyte(PAGE_READTOCACHE);
	SPI_Readbyte(DUMMY_BYTE);
	SPI_Readbyte((uint8_t) (_ulSrcPageNo >> 8));
	SPI_Readbyte((uint8_t) (_ulSrcPageNo & 0x00ff));
	SPI_CS_DISABLE;
	while ((NAND_GetFeatures(STATUS_REG) & 0x01) == 0x01);
	wrap = NAND_GetFeatures(STATUS_REG) >> 4;
	wrap &= 0x3;
	if(wrap == 2)
	{
		return NAND_FAIL;
	}
	NAND_WriteEnable();
	SPI_CS_ENABLE;
	SPI_Readbyte(PROGRAM_EXEXUTE);
	SPI_Readbyte(DUMMY_BYTE);
	SPI_Readbyte((uint8_t) (_ulTarPageNo >> 8));
	SPI_Readbyte((uint8_t) (_ulTarPageNo & 0x00ff));
	SPI_CS_DISABLE;
	
	/* ������״̬ */	
	int timeout = 0;
	while ((NAND_GetFeatures(STATUS_REG) & 0x01) == 0x01) // waiting for SPI flash ready
	{
		timeout ++;
		if(timeout > TIME_OUT_VALUE)
			return NAND_FAIL;
	}
	if(NAND_GetFeatures(STATUS_REG) & 0x08)
	{
		return NAND_FAIL;
	}
	return NAND_OK;
}

/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_PageCopyBackEx
*	����˵��: ��һҳ���ݸ��Ƶ�����һ��ҳ,������Ŀ��ҳ�еĲ������ݡ�Դҳ��Ŀ��ҳ����ͬΪż��ҳ��ͬΪ����ҳ��
*	��    �Σ�- _ulSrcPageNo: Դҳ��
*             - _ulTarPageNo: Ŀ��ҳ��
*			  - _usOffset: ҳ��ƫ�Ƶ�ַ��pBuf�����ݽ�д�������ַ��ʼ��Ԫ
*			  - _pBuf: ���ݻ�����
*			  - _usSize: ���ݴ�С
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*
*	˵    ���������ֲ��Ƽ�����ҳ����֮ǰ����У��Դҳ��λУ�飬������ܻ����λ���󡣱�����δʵ�֡�
*
*********************************************************************************************************
*/
static uint8_t FSMC_NAND_PageCopyBackEx(uint32_t _ulSrcPageNo, uint32_t _ulTarPageNo, uint8_t *_pBuf, uint16_t _usOffset, uint16_t _usSize)
{
	int i;
	uint8_t wrap;
	// Read it to cache
	SPI_CS_ENABLE;
	SPI_Readbyte(PAGE_READTOCACHE);
	SPI_Readbyte(DUMMY_BYTE);
	SPI_Readbyte((uint8_t) (_ulSrcPageNo >> 8));
	SPI_Readbyte((uint8_t) (_ulSrcPageNo & 0x00ff));
	SPI_CS_DISABLE;
	while ((NAND_GetFeatures(STATUS_REG) & 0x01) == 0x01);
	wrap = NAND_GetFeatures(STATUS_REG) >> 4;
	wrap &= 0x3;
	if(wrap == 2)
	{
		return NAND_FAIL;
	}
	
	SPI_CS_ENABLE;
	SPI_Readbyte(PROGRAM_LOAD_RANDOM_DATA);
	SPI_Readbyte(_usOffset >> 8);
	SPI_Readbyte(_usOffset & 0xff);
	for (i = 0; i < _usSize; i++)
	{
		SPI_Readbyte(_pBuf[i]);
	}
	SPI_CS_DISABLE; // End of the data input	
	
	NAND_WriteEnable();
	SPI_CS_ENABLE;
	SPI_Readbyte(PROGRAM_EXEXUTE);
	SPI_Readbyte(DUMMY_BYTE);
	SPI_Readbyte((uint8_t) (_ulTarPageNo >> 8));
	SPI_Readbyte((uint8_t) (_ulTarPageNo & 0x00ff));
	SPI_CS_DISABLE;
	
	/* ������״̬ */	
	int timeout = 0;
	while ((NAND_GetFeatures(STATUS_REG) & 0x01) == 0x01) // waiting for SPI flash ready
	{
		timeout ++;
		if(timeout > TIME_OUT_VALUE)
			return NAND_FAIL;
	}
	if(NAND_GetFeatures(STATUS_REG) & 0x08)
	{
		return NAND_FAIL;
	}
	return NAND_OK;
}

/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_WritePage
*	����˵��: дһ��������NandFlashָ��ҳ���ָ��λ�ã�д������ݳ��Ȳ�����һҳ�Ĵ�С��
*	��    �Σ�- _pBuffer: ָ�������д���ݵĻ����� 
*             - _ulPageNo: ҳ�ţ����е�ҳͳһ���룬��ΧΪ��0 - 65535
*			  - _usAddrInPage : ҳ�ڵ�ַ����ΧΪ��0-2111
*             - _usByteCount: д����ֽڸ���
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*********************************************************************************************************
*/
static uint8_t FSMC_NAND_WritePage(uint8_t *_pBuffer, uint32_t _ulPageNo, uint16_t _usAddrInPage, uint16_t _usByteCount)
{
	uint16_t i;
		
	SPI_CS_ENABLE;
	SPI_Readbyte(PROGRAM_LOAD);
	SPI_Readbyte(_usAddrInPage >> 8);
	SPI_Readbyte(_usAddrInPage & 0xff);
	for (i = 0; i < _usByteCount; i++)
	{
		SPI_Readbyte(_pBuffer[i]);
	}
	SPI_CS_DISABLE; // End of the data input
	
	NAND_WriteEnable();
	//dbg("Execute Programming in page %d\r\n", p);
	SPI_CS_ENABLE;
	SPI_Readbyte(PROGRAM_EXEXUTE);
	SPI_Readbyte(DUMMY_BYTE);
	SPI_Readbyte((uint8_t) (_ulPageNo >> 8));
	SPI_Readbyte((uint8_t) (_ulPageNo & 0x00ff));
	SPI_CS_DISABLE;
	int timeout = 0;
	while ((NAND_GetFeatures(STATUS_REG) & 0x01) == 0x01) // waiting for SPI flash ready
	{
		timeout ++;
		if(timeout > TIME_OUT_VALUE)
			return NAND_FAIL;
	}
	if(NAND_GetFeatures(STATUS_REG) & 0x08)
	{
		return NAND_FAIL;
	}
	return NAND_OK;	
}

/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_ReadPage
*	����˵��: ��NandFlashָ��ҳ���ָ��λ�ö�һ�����ݣ����������ݳ��Ȳ�����һҳ�Ĵ�С��
*	��    �Σ�- _pBuffer: ָ�������д���ݵĻ����� 
*             - _ulPageNo: ҳ�ţ����е�ҳͳһ���룬��ΧΪ��0 - 65535
*			  - _usAddrInPage : ҳ�ڵ�ַ����ΧΪ��0-2111
*             - _usByteCount: д����ֽڸ���
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*********************************************************************************************************
*/
static uint8_t FSMC_NAND_ReadPage(uint8_t *_pBuffer, uint32_t _ulPageNo, uint16_t _usAddrInPage, uint16_t _usByteCount)
{
	uint16_t i;
	uint16_t add;
	uint8_t wrap = 0x00;
	SPI_CS_ENABLE;
	SPI_Readbyte(PAGE_READTOCACHE);
	SPI_Readbyte(DUMMY_BYTE);
	SPI_Readbyte((uint8_t) (_ulPageNo >> 8));
	SPI_Readbyte((uint8_t) (_ulPageNo & 0x00ff));
	SPI_CS_DISABLE;
	int timeout = 0;
	while ((NAND_GetFeatures(STATUS_REG) & 0x01) == 0x01) // waiting for SPI flash ready
	{
		timeout ++;
		if(timeout > TIME_OUT_VALUE)
			return -1;
	}
	wrap = NAND_GetFeatures(STATUS_REG) >> 4;
	wrap &= 0x3;
	if(wrap == 2)
	{
		return NAND_FAIL;
	}
	
	/* Read from cache */
	if (_usByteCount <= 16)
	{
		wrap = 0x0c;
	}
	else if (_usByteCount <= 64)
	{
		wrap = 0x08;
	}
	else if (_usByteCount <= 2048)
	{
		wrap = 0x04;
	}
	else if(_usByteCount <= 2112)
	{
		wrap = 0;
	}

	add = (wrap << 12) + _usAddrInPage;

	SPI_CS_ENABLE;

	SPI_Readbyte(READ_FROM_CACHE);
	SPI_Readbyte((uint8_t) (add >> 8));
	SPI_Readbyte((uint8_t) (add & 0x00ff));
	SPI_Readbyte(DUMMY_BYTE);
	for (i = 0; i < _usByteCount; i++)
	{
		_pBuffer[i] = SPI_Readbyte(DUMMY_BYTE);
	}

	SPI_CS_DISABLE;
	return NAND_OK;
}

/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_WriteSpare
*	����˵��: ��1��PAGE��Spare��д������
*	��    �Σ�- _pBuffer: ָ�������д���ݵĻ����� 
*             - _ulPageNo: ҳ�ţ����е�ҳͳһ���룬��ΧΪ��0 - 65535
*			  - _usAddrInSpare : ҳ�ڱ�������ƫ�Ƶ�ַ����ΧΪ��0-63
*             - _usByteCount: д����ֽڸ���
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*********************************************************************************************************
*/
static uint8_t FSMC_NAND_WriteSpare(uint8_t *_pBuffer, uint32_t _ulPageNo, uint16_t _usAddrInSpare, uint16_t _usByteCount)
{
	uint8_t ret;
	if (_usByteCount > NAND_SPARE_AREA_SIZE)
	{
		return NAND_FAIL;
	}
	
	ret = FSMC_NAND_WritePage(_pBuffer, _ulPageNo, NAND_PAGE_SIZE + _usAddrInSpare, _usByteCount);
	/*dbg("Write spare page = %d, blk = %d\r\nHexDump:\r\n", _ulPageNo, _ulPageNo / 64);
	int i;
	for(i = 0; i < _usByteCount; i++)
		dbg("%02x ", _pBuffer[i]);
	dbg("\r\n");*/
	return ret;
}

/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_ReadSpare
*	����˵��: ��1��PAGE��Spare��������
*	��    �Σ�- _pBuffer: ָ�������д���ݵĻ����� 
*             - _ulPageNo: ҳ�ţ����е�ҳͳһ���룬��ΧΪ��0 - 65535
*			  - _usAddrInSpare : ҳ�ڱ�������ƫ�Ƶ�ַ����ΧΪ��0-63
*             - _usByteCount: д����ֽڸ���
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*********************************************************************************************************
*/
static uint8_t FSMC_NAND_ReadSpare(uint8_t *_pBuffer, uint32_t _ulPageNo, uint16_t _usAddrInSpare, uint16_t _usByteCount)
{
	uint8_t ret;
	if (_usByteCount > NAND_SPARE_AREA_SIZE)
	{
		return NAND_FAIL;
	}

	ret = FSMC_NAND_ReadPage(_pBuffer, _ulPageNo, NAND_PAGE_SIZE + _usAddrInSpare, _usByteCount);
	/*dbg("Read spare page = %d, blk = %d\r\nHexDump:\r\n", _ulPageNo, _ulPageNo / 64);
	int i;
	for(i = 0; i < _usByteCount; i++)
		dbg("%02x ", _pBuffer[i]);
	dbg("\r\n");*/
	return ret;
}

/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_WriteData
*	����˵��: ��1��PAGE����������д������
*	��    �Σ�- _pBuffer: ָ�������д���ݵĻ����� 
*             - _ulPageNo: ҳ�ţ����е�ҳͳһ���룬��ΧΪ��0 - 65535
*			  - _usAddrInPage : ҳ����������ƫ�Ƶ�ַ����ΧΪ��0-2047
*             - _usByteCount: д����ֽڸ���
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*********************************************************************************************************
*/
static uint8_t FSMC_NAND_WriteData(uint8_t *_pBuffer, uint32_t _ulPageNo, uint16_t _usAddrInPage, uint16_t _usByteCount)
{
	if (_usByteCount > NAND_PAGE_SIZE)
	{
		return NAND_FAIL;
	}
	
	return FSMC_NAND_WritePage(_pBuffer, _ulPageNo, _usAddrInPage, _usByteCount);
}
 
/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_ReadData
*	����˵��: ��1��PAGE�������ݵ�����
*	��    �Σ�- _pBuffer: ָ�������д���ݵĻ����� 
*             - _ulPageNo: ҳ�ţ����е�ҳͳһ���룬��ΧΪ��0 - 65535
*			  - _usAddrInPage : ҳ����������ƫ�Ƶ�ַ����ΧΪ��0-2047
*             - _usByteCount: д����ֽڸ���
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*********************************************************************************************************
*/
static uint8_t FSMC_NAND_ReadData(uint8_t *_pBuffer, uint32_t _ulPageNo, uint16_t _usAddrInPage, uint16_t _usByteCount)
{
	if (_usByteCount > NAND_PAGE_SIZE)
	{
		return NAND_FAIL;
	}
	
	return FSMC_NAND_ReadPage(_pBuffer, _ulPageNo, _usAddrInPage, _usByteCount);
}

/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_EraseBlock
*	����˵��: ����NAND Flashһ���飨block��
*	��    �Σ�- _ulBlockNo: ��ţ���ΧΪ��0 - 1023
*	�� �� ֵ: NAND����״̬�������¼���ֵ��
*             - NAND_TIMEOUT_ERROR  : ��ʱ����
*             - NAND_READY          : �����ɹ�
*********************************************************************************************************
*/
static uint8_t FSMC_NAND_EraseBlock(uint32_t _ulBlockNo)
{
	_ulBlockNo = _ulBlockNo * NAND_BLOCK_SIZE;
	NAND_WriteEnable();
	SPI_CS_ENABLE;
	SPI_Readbyte(BLOCK_ERASE);
	SPI_Readbyte(DUMMY_BYTE);
	SPI_Readbyte((uint8_t) (_ulBlockNo >> 8));
	SPI_Readbyte((uint8_t) (_ulBlockNo & 0x00ff));
	SPI_CS_DISABLE;
	int timeout = 0;
	while ((NAND_GetFeatures(STATUS_REG) & 0x01) == 0x01) // waiting for SPI flash ready
	{
		timeout ++;
		if(timeout > TIME_OUT_VALUE)
			return NAND_TIMEOUT_ERROR;
	} 
	if(NAND_GetFeatures(STATUS_REG) & 0x04)
	{
		return NAND_TIMEOUT_ERROR;
	}
	return NAND_READY;
}

/*
*********************************************************************************************************
*	�� �� ��: FSMC_NAND_Reset
*	����˵��: ��λNAND Flash
*	��    �Σ���
*	�� �� ֵ: ��
*********************************************************************************************************
*/
static uint8_t FSMC_NAND_Reset(void)
{
	SPI_CS_ENABLE;
	SPI_Readbyte(0xff);
	SPI_CS_DISABLE;
	return NAND_OK;
}
/*
*********************************************************************************************************
*	�� �� ��: NAND_Init
*	����˵��: ��ʼ��NAND Flash�ӿ�
*	��    �Σ���
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*********************************************************************************************************
*/
uint8_t NAND_Init(void)
{
	uint8_t Status;

	FSMC_NAND_Init();			/* ����FSMC��GPIO����NAND Flash�ӿ� */
	
	FSMC_NAND_Reset();			/* ͨ����λ���λNAND Flash����״̬ */
	#if 1
	int i;
	for(i = 0; i < 1024; i++)
		FSMC_NAND_EraseBlock(i);	
	
	NAND_MarkBadBlock(966);		
	NAND_MarkBadBlock(965);	
	NAND_MarkBadBlock(964);	
	NAND_MarkBadBlock(963);	
	NAND_MarkBadBlock(962);	
	NAND_MarkBadBlock(961);	
	NAND_MarkBadBlock(960);
	NAND_MarkBadBlock(138);			
	NAND_MarkBadBlock(967);
	NAND_Format(0);
	#endif
	#if 0
	int i;
	uint8_t test[4]={0x55, 0xaa, 0xa5, 0x5a};
	FSMC_NAND_EraseBlock(8);
	NAND_Format(0);
	
	/*if(FSMC_NAND_WriteSpare(test, 8*64, 0, 4))
		dbg("Error in writing spare\r\n");	*/
	if(FSMC_NAND_ReadSpare(test, 8*64, 0, 4))
		dbg("Error in reading spare\r\n");
	dbg("Test read spare \r\n");
	for(i = 0; i < 4; i++)
		dbg("%02x ", test[i]);
	dbg("\r\n");
	NAND_DispBadBlockInfo();
	while(1);
	#endif
	Status = NAND_BuildLUT();	/* ���������� LUT = Look up table */
	if(Status)
	{
		dbg("Empty Flash, format it!\r\n");
		NAND_Format(0);//��ʽ��
		dbg("Formatted\r\n");
		NAND_DispBadBlockInfo(); 
		Status = NAND_BuildLUT();
		while(Status);//������˾���������
		dbg("Okay\r\n");
	}
	
	NAND_DispBadBlockInfo();
	dbg("Capacity = %d\r\n", (int)NAND_FormatCapacity());

	return Status;
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_WriteToNewBlock
*	����˵��: ���ɿ�����ݸ��Ƶ��¿飬�����µ����ݶ�д������¿�
*	��    �Σ�	_ulPhyPageNo : Դҳ��
*				_pWriteBuf �� ���ݻ�����
*				_usOffset �� ҳ��ƫ�Ƶ�ַ
*				_usSize �����ݳ��ȣ�������4�ֽڵ�������
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*********************************************************************************************************
*/
uint8_t NAND_WriteToNewBlock(uint32_t _ulPhyPageNo, uint8_t *_pWriteBuf, uint16_t _usOffset, uint16_t _usSize)
{
	uint16_t n, i;
	uint16_t usNewBlock;
	uint16_t ulSrcBlock;
	uint16_t usOffsetPageNo;

	ulSrcBlock = _ulPhyPageNo / NAND_BLOCK_SIZE;		/* ��������ҳ�ŷ��ƿ�� */
	usOffsetPageNo = _ulPhyPageNo % NAND_BLOCK_SIZE;	/* ��������ҳ�ż�������ҳ���ڿ���ƫ��ҳ�� */
	/* ����ѭ����Ŀ���Ǵ���Ŀ���Ϊ�������� */
	for (n = 0; n < 10; n++)
	{
		/* �������ȫ0xFF�� ����ҪѰ��һ�����п��ÿ飬����ҳ�ڵ�����ȫ���Ƶ��¿��У�Ȼ���������� */
		usNewBlock = NAND_FindFreeBlock();	/* �����һ��Block��ʼ����Ѱһ�����ÿ� */
		if (usNewBlock >= NAND_BLOCK_COUNT)
		{
			return NAND_FAIL;	/* ���ҿ��п�ʧ�� */
		}
		
		/* ʹ��page-copy���ܣ�����ǰ�飨usPBN��������ȫ�����Ƶ��¿飨usNewBlock�� */
		for (i = 0; i < NAND_BLOCK_SIZE; i++)
		{
			if (i == usOffsetPageNo)
			{
				/* ���д��������ڵ�ǰҳ������Ҫʹ�ô�������ݵ�Copy-Back���� */
				if (FSMC_NAND_PageCopyBackEx(ulSrcBlock * NAND_BLOCK_SIZE + i, usNewBlock * NAND_BLOCK_SIZE + i,
					_pWriteBuf, _usOffset, _usSize) == NAND_FAIL)
				{
					dbg("Copy-Back-Ex error on block %d\r\n", usNewBlock);
					NAND_MarkBadBlock(usNewBlock);	/* ���¿���Ϊ���� */
					NAND_BuildLUT();				/* �ؽ�LUT�� */
					break;
				}
			}
			else
			{
				/* ʹ��NAND Flash �ṩ����ҳCopy-Back���ܣ�����������߲���Ч�� */
				if (FSMC_NAND_PageCopyBack(ulSrcBlock * NAND_BLOCK_SIZE + i, 
					usNewBlock * NAND_BLOCK_SIZE + i) == NAND_FAIL)
				{
					dbg("Copy-Back error on block %d\r\n", usNewBlock);
					NAND_MarkBadBlock(usNewBlock);	/* ���¿���Ϊ���� */
					NAND_BuildLUT();				/* �ؽ�LUT�� */
					break;
				}
			}
		}
		/* Ŀ�����³ɹ� */
		if (i == NAND_BLOCK_SIZE)
		{
			/* ����¿�Ϊ���ÿ� */
			if (NAND_MarkUsedBlock(usNewBlock) == NAND_FAIL)
			{
				dbg("Mark Used Block on block %d\r\n", usNewBlock);
				NAND_MarkBadBlock(usNewBlock);	/* ���¿���Ϊ���� */
				NAND_BuildLUT();				/* �ؽ�LUT�� */
				continue;
			}
			
			/* ����ԴBLOCK */
			if (FSMC_NAND_EraseBlock(ulSrcBlock) != NAND_READY)
			{
				dbg("Erase error on block %d\r\n", ulSrcBlock);
				NAND_MarkBadBlock(ulSrcBlock);	/* ��Դ����Ϊ���� */
				NAND_BuildLUT();				/* �ؽ�LUT�� */
				continue;
			}
			NAND_BuildLUT();				/* �ؽ�LUT�� */
			break;
		}
	}
	
	return NAND_OK;	/* д��ɹ� */
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_Write
*	����˵��: дһ������
*	��    �Σ�	_MemAddr : �ڴ浥Ԫƫ�Ƶ�ַ
*				_pReadbuff ����Ŵ�д���ݵĻ�������ָ��
*				_usSize �����ݳ��ȣ�������4�ֽڵ�������
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*********************************************************************************************************
*/
uint8_t NAND_Write(uint32_t _ulMemAddr, uint32_t *_pWriteBuf, uint16_t _usSize)
{
	uint16_t usPBN;			/* ������ */
	uint32_t ulPhyPageNo;	/* ����ҳ�� */
	uint16_t usAddrInPage;	/* ҳ��ƫ�Ƶ�ַ */
	uint32_t ulTemp;

	/* ���ݳ��ȱ�����4�ֽ������� */
	if ((_usSize % 4) != 0)
	{
		return NAND_FAIL;
	}
	/* ���ݳ��Ȳ��ܳ���512�ֽ�(��ѭ Fat��ʽ) */
	if (_usSize > 512)
	{
		//return NAND_FAIL;	
	}

	usPBN = NAND_AddrToPhyBlockNo(_ulMemAddr);	/* ��ѯLUT���������� */

	ulTemp = _ulMemAddr % (NAND_BLOCK_SIZE * NAND_PAGE_SIZE);
	ulPhyPageNo = usPBN * NAND_BLOCK_SIZE + ulTemp / NAND_PAGE_SIZE;	/* ��������ҳ�� */
	usAddrInPage = ulTemp % NAND_PAGE_SIZE;	/* ����ҳ��ƫ�Ƶ�ַ */
	
	/* �������������ݣ��ж��Ƿ�ȫFF */
	if (FSMC_NAND_ReadData(s_ucTempBuf, ulPhyPageNo, usAddrInPage, _usSize) == NAND_FAIL)
	{
		return NAND_FAIL;	/* ��NAND Flashʧ�� */
	}
	/*�������ȫ0xFF, �����ֱ��д�룬������� */
	if (NAND_IsBufOk(s_ucTempBuf, _usSize, 0xFF))
	{
		if (FSMC_NAND_WriteData((uint8_t *)_pWriteBuf, ulPhyPageNo, usAddrInPage, _usSize) == NAND_FAIL)
		{
			/* ������д�뵽����һ���飨���п飩 */
			return NAND_WriteToNewBlock(ulPhyPageNo, (uint8_t *)_pWriteBuf, usAddrInPage, _usSize);
		}
		
		/* ��Ǹÿ����� */
		if (NAND_MarkUsedBlock(ulPhyPageNo) == NAND_FAIL)
		{
			/* ���ʧ�ܣ�������д�뵽����һ���飨���п飩 */
			return NAND_WriteToNewBlock(ulPhyPageNo, (uint8_t *)_pWriteBuf, usAddrInPage, _usSize);
		}	
		return NAND_OK;	/* д��ɹ� */
	}
	
	/* ������д�뵽����һ���飨���п飩 */
	return NAND_WriteToNewBlock(ulPhyPageNo, (uint8_t *)_pWriteBuf, usAddrInPage, _usSize);
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_Read
*	����˵��: ��һ������
*	��    �Σ�	_MemAddr : �ڴ浥Ԫƫ�Ƶ�ַ
*				_pReadbuff ����Ŷ������ݵĻ�������ָ��
*				_usSize �����ݳ��ȣ�������4�ֽڵ�������
*	�� �� ֵ: ִ�н����
*				- NAND_FAIL ��ʾʧ��
*				- NAND_OK ��ʾ�ɹ�
*********************************************************************************************************
*/
uint8_t NAND_Read(uint32_t _ulMemAddr, uint32_t *_pReadBuf, uint16_t _usSize)
{
	uint16_t usPBN;			/* ������ */
	uint32_t ulPhyPageNo;	/* ����ҳ�� */
	uint16_t usAddrInPage;	/* ҳ��ƫ�Ƶ�ַ */
	uint32_t ulTemp;

	/* ���ݳ��ȱ�����4�ֽ������� */
	if ((_usSize % 4) != 0)
	{
		return NAND_FAIL;
	}

	usPBN = NAND_AddrToPhyBlockNo(_ulMemAddr);	/* ��ѯLUT���������� */
	if (usPBN >= NAND_BLOCK_COUNT)
	{
		/* û�и�ʽ����usPBN = 0xFFFF */
		return NAND_FAIL;
	}

	ulTemp = _ulMemAddr % (NAND_BLOCK_SIZE * NAND_PAGE_SIZE);
	ulPhyPageNo = usPBN * NAND_BLOCK_SIZE + ulTemp / NAND_PAGE_SIZE;	/* ��������ҳ�� */
	usAddrInPage = ulTemp % NAND_PAGE_SIZE;	/* ����ҳ��ƫ�Ƶ�ַ */
	
	if (FSMC_NAND_ReadData((uint8_t *)_pReadBuf, ulPhyPageNo, usAddrInPage, _usSize) == NAND_FAIL)
	{
		return NAND_FAIL;	/* ��NAND Flashʧ�� */
	}
	
	/* �ɹ� */
	return NAND_OK;
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_BuildLUT
*	����˵��: ���ڴ��д�����������
*	��    �Σ�ZoneNbr ������
*	�� �� ֵ: NAND_OK�� �ɹ��� 	NAND_FAIL��ʧ��
*********************************************************************************************************
*/
static uint8_t NAND_BuildLUT(void)
{
	uint16_t i;
	uint8_t buf[VALID_SPARE_SIZE];
	uint16_t usLBN;	/* �߼���� */
	
	/* */
	for (i = 0; i < NAND_BLOCK_COUNT; i++)
	{
		s_usLUT[i] = 0xFFFF;	/* �����Чֵ�������ؽ�LUT���ж�LUT�Ƿ���� */
	}
	for (i = 0; i < NAND_BLOCK_COUNT; i++)
	{
		/* ����ǻ��� */
		if(NAND_IsBadBlock(i)) continue;
		/* ��ÿ����ĵ�1��PAGE��ƫ�Ƶ�ַΪLBN0_OFFSET������ */
		if(FSMC_NAND_ReadSpare(buf, i * NAND_BLOCK_SIZE, 0, VALID_SPARE_SIZE)) continue;
		/* ����Ǻÿ飬���¼LBN0 LBN1 */
		if (buf[BI_OFFSET] == 0xFF)	
		{
			usLBN = buf[LBN0_OFFSET] + buf[LBN1_OFFSET] * 256;	/* ����������߼���� */
			if (usLBN < NAND_BLOCK_COUNT)
			{
				/* ����Ѿ��Ǽǹ��ˣ����ж�Ϊ�쳣 */
				if (s_usLUT[usLBN] != 0xFFFF)
				{
					dbg("No signed on block %d, LBN = %d\r\n", i, usLBN);
					return NAND_FAIL;
				}

				s_usLUT[usLBN] = i;	/* ����LUT�� */
			}
		}
	}
	
	/* LUT������ϣ�����Ƿ���� */
	for (i = 0; i < NAND_BLOCK_COUNT; i++)
	{
		if (s_usLUT[i] >= NAND_BLOCK_COUNT)
		{
			s_usValidDataBlockCount = i;
			break;
		}
	}
	if (s_usValidDataBlockCount < 100)
	{
		/* ���� ������Ч�߼����С��100��������û�и�ʽ�� */
		return NAND_FAIL;	
	}
	for (; i < s_usValidDataBlockCount; i++)
	{
		if (s_usLUT[i] != 0xFFFF)
		{
			return NAND_FAIL;	/* ����LUT���߼���Ŵ�����Ծ���󣬿�����û�и�ʽ�� */
		}
	}
	
	/* �ؽ�LUT���� */
	return NAND_OK;
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_AddrToPhyBlockNo
*	����˵��: �ڴ��߼���ַת��Ϊ������
*	��    �Σ�_ulMemAddr���߼��ڴ��ַ
*	�� �� ֵ: ����ҳ�ţ� ����� 0xFFFFFFFF ���ʾ����
*********************************************************************************************************
*/
static uint16_t NAND_AddrToPhyBlockNo(uint32_t _ulMemAddr)
{
	uint16_t usLBN;		/* �߼���� */
	uint16_t usPBN;		/* ������ */
	
	usLBN = _ulMemAddr / (NAND_BLOCK_SIZE * NAND_PAGE_SIZE);	/* �����߼���� */
	/* ����߼���Ŵ�����Ч�����ݿ������̶�����0xFFFF, ���øú����Ĵ���Ӧ�ü������ִ��� */
	if (usLBN >= s_usValidDataBlockCount)
	{
		return 0xFFFF;
	}
	/* ��ѯLUT����������� */
	usPBN = s_usLUT[usLBN];
	return usPBN;
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_FindFreeBlock
*	����˵��: �����һ���鿪ʼ������һ�����õĿ顣
*	��    �Σ�ZoneNbr ������
*	�� �� ֵ: ��ţ������0xFFFF��ʾʧ��
*********************************************************************************************************
*/
static uint16_t NAND_FindFreeBlock (void)
{
	uint16_t i;
	uint16_t n;

	n = NAND_BLOCK_COUNT - 1;
	for (i = 0; i < NAND_BLOCK_COUNT; i++)
	{
		if (NAND_IsFreeBlock(n))
		{
			return n;
		}
		n--;
	}
	return 0xFFFF;
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_IsBufOk
*	����˵��: �ж��ڴ滺�����������Ƿ�ȫ��Ϊָ��ֵ
*	��    �Σ�- _pBuf : ���뻺����
*			  - _ulLen : ����������
*			  - __ucValue : ������ÿ����Ԫ����ȷ��ֵ
*	�� �� ֵ: 1 ��ȫ����ȷ�� 0 ������ȷ
*********************************************************************************************************
*/
static uint8_t NAND_IsBufOk(uint8_t *_pBuf, uint32_t _ulLen, uint8_t _ucValue)
{
	uint32_t i;
	
	for (i = 0; i < _ulLen; i++)
	{
		if (_pBuf[i] != _ucValue)
		{
			return 0;
		}
	}
	
	return 1;
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_IsBadBlock
*	����˵��: ���ݻ����Ǽ��NAND Flashָ���Ŀ��Ƿ񻵿�
*	��    ��: _ulBlockNo ����� 0 - 1023 ������128M�ֽڣ�2K Page��NAND Flash����1024���飩
*	�� �� ֵ: 0 ���ÿ���ã� 1 ���ÿ��ǻ���
*********************************************************************************************************
*/
static uint8_t NAND_IsBadBlock(uint32_t _ulBlockNo)
{
	uint8_t ucFlag;
	
	/* ���NAND Flash����ǰ�Ѿ���עΪ�����ˣ������Ϊ�ǻ��� */
	if(FSMC_NAND_ReadSpare(&ucFlag, _ulBlockNo * NAND_BLOCK_SIZE, BI_OFFSET, 1))
		return 1;
	if (ucFlag != 0xFF)
	{
		return 1;		
	}

	if(FSMC_NAND_ReadSpare(&ucFlag, _ulBlockNo * NAND_BLOCK_SIZE + 1, BI_OFFSET, 1))
		return 1; 
	if (ucFlag != 0xFF)
	{
		return 1;		
	}	
	return 0;	/* �Ǻÿ� */
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_IsFreeBlock
*	����˵��: ���ݻ����Ǻ�USED��־����Ƿ���ÿ�
*	��    ��: _ulBlockNo ����� 0 - 1023 ������128M�ֽڣ�2K Page��NAND Flash����1024���飩
*	�� �� ֵ: 1 ���ÿ���ã� 0 ���ÿ��ǻ��������ռ��
*********************************************************************************************************
*/
static uint8_t NAND_IsFreeBlock(uint32_t _ulBlockNo)
{
	uint8_t ucFlag;

	/* ���NAND Flash����ǰ�Ѿ���עΪ�����ˣ������Ϊ�ǻ��� */
	if (NAND_IsBadBlock(_ulBlockNo))
	{
		return 0;
	}
	
	FSMC_NAND_ReadPage(&ucFlag, _ulBlockNo * NAND_BLOCK_SIZE, USED_OFFSET, 1);
	if (ucFlag == 0xFF)
	{
		return 1;	
	}
	return 0;
}
	
/*
*********************************************************************************************************
*	�� �� ��: NAND_ScanBlock
*	����˵��: ɨ�����NAND Flashָ���Ŀ�
*			��ɨ������㷨��
*			1) ��1���飨�������������ͱ����������������������Ƿ�ȫ0xFF, ��ȷ�Ļ��������ԸĿ飬����ÿ�
				�ǻ���,��������
*			2) ��ǰ��д��ȫ 0x00��Ȼ���ȡ��⣬��ȷ�Ļ��������ԸĿ飬�����˳�
*			3) �ظ��ڣ�2���������ѭ��������50�ζ�û�з���������ô�ÿ�����,�������أ�����ÿ��ǻ��飬
*				��������
*			��ע�⡿
*			1) �ú���������Ϻ󣬻�ɾ�������������ݣ�����Ϊȫ0xFF;
*			2) �ú������˲������������⣬Ҳ�Ա������������в��ԡ�
*			3) ��д����ѭ���������Ժ�ָ����#define BAD_BALOK_TEST_CYCLE 50
*	��    �Σ�_ulPageNo ��ҳ�� 0 - 65535 ������128M�ֽڣ�2K Page��NAND Flash����1024���飩
*	�� �� ֵ: NAND_OK ���ÿ���ã� NAND_FAIL ���ÿ��ǻ���
*********************************************************************************************************
*/
uint8_t NAND_ScanBlock(uint32_t _ulBlockNo)
{
	uint32_t i, k;
	uint32_t ulPageNo;
	
	#if 1	
	/* ���NAND Flash����ǰ�Ѿ���עΪ�����ˣ������Ϊ�ǻ��� */
	if (NAND_IsBadBlock(_ulBlockNo))
	{
		return NAND_FAIL;
	}
	#endif
	
	/* ����Ĵ��뽫ͨ��������������̵ķ�ʽ������NAND Flashÿ����Ŀɿ��� */
	memset(s_ucTempBuf, 0x00, NAND_PAGE_SIZE);
	for (i = 0; i < BAD_BALOK_TEST_CYCLE; i++)
	{
		/* ��1������������� */	
		if (FSMC_NAND_EraseBlock(_ulBlockNo) != NAND_READY)
		{
			//dbg("Erase error blk %d cycle %d\r\n", _ulBlockNo, i);
			return NAND_FAIL;
		}
		
		/* ��2������������ÿ��page�����ݣ����ж��Ƿ�ȫ0xFF */
		ulPageNo = _ulBlockNo * NAND_BLOCK_SIZE;	/* ����ÿ��1��ҳ��ҳ�� */
		for (k = 0; k < NAND_BLOCK_SIZE; k++)
		{
			/* ������ҳ���� */
			FSMC_NAND_ReadPage(s_ucTempBuf, ulPageNo, 0, NAND_PAGE_SIZE);

			/* �жϴ洢��Ԫ�ǲ���ȫ0xFF */
			if (NAND_IsBufOk(s_ucTempBuf, NAND_PAGE_SIZE, 0xFF) != 1)
			{
				//dbg("Isn't empty blk %d cycle %d\r\n", _ulBlockNo, i);
				return NAND_FAIL;
			}
			
			ulPageNo++;		/* ����д��һ��ҳ */
		}
		
		/* ��2����дȫ0���������ж��Ƿ�ȫ0 */
		ulPageNo = _ulBlockNo * NAND_BLOCK_SIZE;	/* ����ÿ��1��ҳ��ҳ�� */
		for (k = 0; k < NAND_BLOCK_SIZE; k++)
		{
			/* ���buf[]������Ϊȫ0,��д��NAND Flash */
			memset(s_ucTempBuf, 0x00, NAND_PAGE_SIZE);
			if (FSMC_NAND_WritePage(s_ucTempBuf, ulPageNo, 0, NAND_PAGE_SIZE) != NAND_OK)
			{
				//dbg("isn't full blk %d cycle %d on write\r\n", _ulBlockNo, i);
				return NAND_FAIL;
			}
			
			/* ������ҳ����, �жϴ洢��Ԫ�ǲ���ȫ0x00 */
			FSMC_NAND_ReadPage(s_ucTempBuf, ulPageNo, 0, NAND_PAGE_SIZE);
			if (NAND_IsBufOk(s_ucTempBuf, NAND_PAGE_SIZE, 0x00) != 1)
			{
				//dbg("isn't full blk %d cycle %d on read\r\n", _ulBlockNo, i);				
				return NAND_FAIL;
			}
			
			ulPageNo++;		/* ����һ��ҳ */						
		}
	}
	
	/* ���һ�������������� */
	if (FSMC_NAND_EraseBlock(_ulBlockNo) != NAND_READY)
	{
		return NAND_FAIL;
	} 
	ulPageNo = _ulBlockNo * NAND_BLOCK_SIZE;	/* ����ÿ��1��ҳ��ҳ�� */
	for (k = 0; k < NAND_BLOCK_SIZE; k++)
	{
		/* ������ҳ���� */
		FSMC_NAND_ReadPage(s_ucTempBuf, ulPageNo, 0, NAND_PAGE_SIZE);

		/* �жϴ洢��Ԫ�ǲ���ȫ0xFF */
		if (NAND_IsBufOk(s_ucTempBuf, NAND_PAGE_SIZE, 0xFF) != 1)
		{
			//dbg("isn't empty blk %d cycle %d on check\r\n", _ulBlockNo, i);
			return NAND_FAIL;
		}
		
		ulPageNo++;		/* ����д��һ��ҳ */
	}	
	
	return NAND_OK;
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_MarkUsedBlock
*	����˵��: ���NAND Flashָ���Ŀ�Ϊ���ÿ�
*	��    ��: _ulBlockNo ����� 0 - 1023 ������128M�ֽڣ�2K Page��NAND Flash����1024���飩
*	�� �� ֵ: NAND_OK:��ǳɹ��� NAND_FAIL�����ʧ�ܣ��ϼ����Ӧ�ý��л��鴦��
*********************************************************************************************************
*/
static uint8_t NAND_MarkUsedBlock(uint32_t _ulBlockNo)
{								   
	uint32_t ulPageNo;
	uint8_t ucFlag;
	
	/* �����ĵ�1��ҳ�� */
	ulPageNo = _ulBlockNo * NAND_BLOCK_SIZE;	/* ����ÿ��1��ҳ��ҳ�� */
	
	/* ���ڵ�1��page�������ĵ�6���ֽ�д���0xFF���ݱ�ʾ���� */
	ucFlag = NAND_USED_BLOCK_FLAG;
	if (FSMC_NAND_WriteSpare(&ucFlag, ulPageNo, USED_OFFSET, 1) == NAND_FAIL)
	{
		/* ������ʧ�ܣ�����Ҫ��ע�����Ϊ���� */
		return NAND_FAIL;
	}
	return NAND_OK;
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_MarkBadBlock
*	����˵��: ���NAND Flashָ���Ŀ�Ϊ����
*	��    ��: _ulBlockNo ����� 0 - 1023 ������128M�ֽڣ�2K Page��NAND Flash����1024���飩
*	�� �� ֵ: �̶�NAND_OK
*********************************************************************************************************
*/
static void NAND_MarkBadBlock(uint32_t _ulBlockNo)
{								   
	uint32_t ulPageNo;
	uint8_t ucFlag;
	dbg("Mark bad block %d\r\n", _ulBlockNo);
	/* �����ĵ�1��ҳ�� */
	ulPageNo = _ulBlockNo * NAND_BLOCK_SIZE;	/* ����ÿ��1��ҳ��ҳ�� */
	
	/* ���ڵ�1��page�������ĵ�6���ֽ�д���0xFF���ݱ�ʾ���� */
	ucFlag = NAND_BAD_BLOCK_FLAG;
	if (FSMC_NAND_WriteSpare(&ucFlag, ulPageNo, BI_OFFSET, 1) == NAND_FAIL)
	{
		/* �����1��ҳ���ʧ�ܣ����ڵ�2��ҳ��� */
		FSMC_NAND_WriteSpare(&ucFlag, ulPageNo + 1, BI_OFFSET, 1);
	}
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_Format
*	����˵��: NAND Flash��ʽ�����������е����ݣ��ؽ�LUT
*	��    �Σ���
*	�� �� ֵ: NAND_OK : �ɹ��� NAND_Fail ��ʧ�ܣ�һ���ǻ����������ർ�£�
*********************************************************************************************************
*/
uint8_t NAND_Format(uint8_t mode)
{
	uint16_t i, n;
	uint8_t buffer[4]={0xff, 0xff, 0, 0};
	uint16_t usGoodBlockCount;

	/* ����ÿ���� */
	usGoodBlockCount = 0;
	if(mode == 1)
	{//Safest format
		for (i = 0; i < NAND_BLOCK_COUNT; i++)
		{
			/* ����Ǻÿ飬����� */
			if (NAND_ScanBlock(i) == NAND_OK)
			{
				usGoodBlockCount++;
				dbg("Tested block %d\r\n", i);
			}
			else
			{
				dbg("Bad block %d\r\n", i);
				NAND_MarkBadBlock(i);
			}
		}
	}
	else
	{
		for (i = 0; i < NAND_BLOCK_COUNT; i++)
		{
			/* ����Ǻÿ飬����� */
			if (NAND_IsBadBlock(i) == NAND_OK)
			{
				FSMC_NAND_EraseBlock(i);
				usGoodBlockCount++;
			}
		}		
	}
		
	/* ����ÿ����������100����NAND Flash���� */
	if (usGoodBlockCount < 100)
	{
		return NAND_FAIL;
	}

	usGoodBlockCount = (usGoodBlockCount * 95) / 100;	/* 98%�ĺÿ����ڴ洢���� */
		
	/* ��������һ�� */
	n = 0; /* ͳ���ѱ�ע�ĺÿ� */ 
	for (i = 0; i < NAND_BLOCK_COUNT; i++)
	{
		if (!NAND_IsBadBlock(i))
		{
			/* ����Ǻÿ飬���ڸÿ�ĵ�1��PAGE��LBN0 LBN1��д��nֵ (ǰ���Ѿ�ִ���˿������ */
			buffer[LBN0_OFFSET] = n & 0xff;
			buffer[LBN1_OFFSET] = n >> 8;
			if(FSMC_NAND_WriteSpare(buffer, i * NAND_BLOCK_SIZE, 0, 4)!=NAND_OK)
			{
				dbg("Bad block in %d\r\n", i);
				FSMC_NAND_EraseBlock(i);
				NAND_MarkBadBlock(i);
				continue;
			}
			dbg("Write LBN %d to PBN %d\r\n",n, i);
			n++;

			/* ���㲢д��ÿ��������ECCֵ ����ʱδ����*/

			if (n == usGoodBlockCount)
			{
				break; 
			}
		}
	}

	NAND_BuildLUT();	/* ��ʼ��LUT�� */
	return NAND_OK;
}

/*
*********************************************************************************************************
*	�� �� ��: NAND_FormatCapacity
*	����˵��: NAND Flash��ʽ�������Ч����
*	��    �Σ���
*	�� �� ֵ: NAND_OK : �ɹ��� NAND_Fail ��ʧ�ܣ�һ���ǻ����������ർ�£�
*********************************************************************************************************
*/
uint32_t NAND_FormatCapacity(void)
{
	uint16_t usCount;
	
	/* �������ڴ洢���ݵ����ݿ��������������Ч������98%������ */
	usCount = (s_usValidDataBlockCount * DATA_BLOCK_PERCENT) / 100;
	
	return (usCount * NAND_BLOCK_SIZE * NAND_PAGE_SIZE);
}
	
/*
*********************************************************************************************************
*	�� �� ��: NAND_DispBadBlockInfo
*	����˵��: ͨ�����ڴ�ӡ��NAND Flash�Ļ�����Ϣ
*	��    �Σ���
*	�� �� ֵ: ��
*********************************************************************************************************
*/
void NAND_DispBadBlockInfo(void)
{
	uint32_t id;
	uint32_t i;
	uint32_t n;
	
	FSMC_NAND_Init();	/* ��ʼ��FSMC */
	
	id = NAND_ReadID();
	
	printf("NAND Flash ID = 0x%04X, Type = ", (int)id);
	if (id == HY27UF081G2A)
	{
		printf("HY27UF081G2A\r\n  1024 Blocks, 64 pages per block, 2048 + 64 bytes per page\r\n");
	}
	else if (id == K9F1G08U0A)
	{
		printf("K9F1G08U0A\r\n  1024 Blocks, 64 pages per block, 2048 + 64 bytes per page\r\n");
	}
	else if (id == K9F1G08U0B)
	{
		printf("K9F1G08U0B\r\n  1024 Blocks, 64 pages per block, 2048 + 64 bytes per page\r\n");
	}
	else if (id == GD5F1GQ4)
	{
		printf("GD5F1GQ4\r\n  1024 Blocks, 64 pages per block, 2048 + 64 bytes per page\r\n");		
	}
	else if (id == GD5F2GQ4)
	{
		printf("GD5F2GQ4\r\n  2048 Blocks, 64 pages per block, 2048 + 64 bytes per page\r\n");		
	}
	else
	{
		printf("unkonow\r\n");
		return;
	}
	
	printf("Block Info :\r\n");
	n = 0;	/* ����ͳ�� */
	for (i = 0; i < NAND_BLOCK_COUNT; i++)
	{
		if (NAND_IsBadBlock(i))
		{
			printf("*");
			n++;
		}
		else
		{
			printf("0");
		}
		
		if (((i + 1) % 8) == 0)
		{
			printf(" ");
		}
		
		if (((i + 1) % 64) == 0)
		{
			printf("\r\n");
		}
	}
	printf("\r\nBad Block Count = %d\r\n",(int) n);
}
