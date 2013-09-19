#include <string.h>
#include "crc16.h"

#define CRC_A 1
#define CRC_B 2

#define LOCK_CHECK_TK_CRC16
/*
 * CRC16 Lookup tables (High and Low Byte) for 4 bits per iteration.
 */
unsigned short CRC16_LookupHigh[16] = {
        0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
        0x81, 0x91, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1
};
unsigned short CRC16_LookupLow[16] = {
        0x00, 0x21, 0x42, 0x63, 0x84, 0xA5, 0xC6, 0xE7,
        0x08, 0x29, 0x4A, 0x6B, 0x8C, 0xAD, 0xCE, 0xEF
};

/*
 * CRC16 "Register". This is implemented as two 8bit values
 */
unsigned char CRC16_High, CRC16_Low;

/*
 * Before each message CRC is generated, the CRC register must be
 * initialised by calling this function
 */
void CRC16_Init(void)
{
	// Initialise the CRC to 0xFFFF for the CCITT specification
	CRC16_High = 0xFF;
	CRC16_Low = 0xFF;
}


void CRC16_Init_Value(unsigned int Value)
{
	CRC16_High = Value>>8;
	CRC16_Low = Value&0x00FF;
}

/*
 * Process 4 bits of the message to update the CRC Value.
 *
 * Note that the data must be in the low nibble of val.
 */
void CRC16_Update4Bits( unsigned char val )
{
	unsigned char	t;

	// Step one, extract the Most significant 4 bits of the CRC register
	t = CRC16_High >> 4;

	// XOR in the Message Data into the extracted bits
	t = t ^ val;

	// Shift the CRC Register left 4 bits
	CRC16_High = (CRC16_High << 4) | (CRC16_Low >> 4);
	CRC16_Low = CRC16_Low << 4;

	// Do the table lookups and XOR the result into the CRC Tables
	CRC16_High = CRC16_High ^ CRC16_LookupHigh[t];
	CRC16_Low = CRC16_Low ^ CRC16_LookupLow[t];
}

/*
 * Process one Message Byte to update the current CRC Value
 */
void CRC16_Update( unsigned char val )
{
	CRC16_Update4Bits( val >> 4 );		// High nibble first
	CRC16_Update4Bits( val & 0x0F );	// Low nibble
}

void CRC16_Calc( const void *buffer,uint8_t len)
{
  unsigned char* data = (uint8_t*)buffer;
  CRC16_Init();
  while(len--){CRC16_Update(*data);data++;}
}

void CRC16_CalcData( unsigned char *data,unsigned char datalen)
{
  while(datalen--){CRC16_Update(*data);data++;}
}

unsigned char CRC16_CheckSector(unsigned char *Block0,unsigned char *Block1,unsigned char *Block2)
{
#ifdef LOCK_CHECK_TK_CRC16
  return 1;
#else
  CRC16_Init();
  CRC16_CalcData(Block0,16);
  CRC16_CalcData(Block1,16);
  CRC16_CalcData(Block2,14);
  return (((Block2[14] == CRC16_High)&&(Block2[15] == CRC16_Low)) || ((Block2[14] == 0)&&(Block2[15] == 0)));
#endif
}

unsigned char  CRC16_CheckBlock(unsigned char *Block)
{
#ifdef LOCK_CHECK_TK_CRC16
  return 1;
#else
  CRC16_Init();
  CRC16_CalcData(Block,14);
  return (((Block[14] == CRC16_High)&&(Block[15] == CRC16_Low)) || ((Block[14] == 0)&&(Block[15] == 0)));
#endif
}

void  CRC16_CalcBlock(void *Block,int low_endian)
{
  unsigned char* data = (unsigned char*)Block;

  CRC16_Init();
  CRC16_CalcData(data,14);
  if(low_endian) {
	data[14] = CRC16_Low;
	data[15] = CRC16_High;
  } else {
	data[14] = CRC16_High;
	data[15] = CRC16_Low;	
  }
}

void  CRC16_CalcSector(void* Sector,int low_endian)
{
  unsigned char* data = (unsigned char*)Sector;

  CRC16_Init();
  CRC16_CalcData(data,48 - 2);
  if(low_endian) {
	data[48-2] = CRC16_Low;
	data[48-1] = CRC16_High;
  } else {
	data[48-2] = CRC16_High;
	data[48-1] = CRC16_Low;
  }
}

/******************************************************************************
	Функция CheckDataCRC16 принимает указатель на данные, считаные из 
	сектора бесконтактной карты (CardSectоrData) и длину данных (DataSize). 
	Подсчитывает контрольную сумму данных и сравнивает с ее двумя последними 
	байтами (два последних байта - контрольная сумма, записанная в секторе 
	карточки), возвращает TRUE (1) если CRC совпало. FALSE(0) если не совпало.
******************************************************************************/
uint8_t CheckDataCRC16(void *data, uint8_t DataSize,int low_endian)
{
	uint8_t *CardSectorData = (uint8_t*)data;

	CRC16_Calc(CardSectorData, (DataSize-2));// Досчитываем CRC по данным сектора Резулат - (контрольная сумма в CRC16_High и CRC16_Low)
	
	if(low_endian) {
		if((CardSectorData[DataSize-2] == CRC16_Low)&&(CardSectorData[DataSize-1] == CRC16_High)) return 1;
		else return 0;
	} else {
		if((CardSectorData[DataSize-1] == CRC16_Low)&&(CardSectorData[DataSize-2] == CRC16_High)) return 1;
		else return 0;
	} 
}

/******************************************************************************
**   Имплементация алгоритма подсчета контрольной суммы CRC16 для карт типа ***
**   DESFire, согласно стандарту ISO/IEC14443A (также ISO/IEC14443B)				***
******************************************************************************/
uint8_t UpdateCrc(uint8_t ch, uint16_t *lpwCrc)
{
	ch = (ch^(uint8_t)((*lpwCrc) & 0x00FF));
	ch = (ch^(ch<<4));
	*lpwCrc = (*lpwCrc >> 8)^((uint16_t)ch << 8)^((uint16_t)ch<<3)^((uint16_t)ch>>4);
	return (uint8_t)(*lpwCrc);
}

/******************************************************************************
	Функция для подсчета контрольной суммы CRC16 ISO14443A (также ISO/IEC14443B)
	CRCType опредиляет тип подсчета ISO14443A или B (имеют разные init value):
	CRC_A - ISO14443A (Init Value = 0x6363)
	CRC_B - ISO14443B (Init Value = 0xFFFF)
	*Data - указатель на массив данных
	Length - длина массива данных для посчета CRC
	TransmitFirst - младший байт результата 
	TransmitSecond - старший байт результата
******************************************************************************/
void ComputeCrc(uint16_t CRCType, uint8_t *Data, uint16_t Length, uint8_t *TransmitFirst, uint8_t *TransmitSecond)
{
	uint8_t chBlock;
	uint16_t wCrc;
	switch(CRCType) 
	{
		case CRC_A:
		wCrc = 0x6363; // ITU-V.41
		break;
		case CRC_B:
		wCrc = 0xFFFF; // ISO 3309
		break;
		default:
		return;
	}
	do 
	{
		chBlock = *Data++;
		UpdateCrc(chBlock, &wCrc);
	} 
	while (--Length);
	if (CRCType == CRC_B)	wCrc = ~wCrc; // ISO 3309
	*TransmitFirst = (uint8_t) (wCrc & 0xFF);
	*TransmitSecond = (uint8_t) ((wCrc >> 8) & 0xFF);
	return;
}
