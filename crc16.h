#ifndef __CRC16_H__
#define __CRC16_H__

#include <boost/cstdint.hpp>
using namespace boost;

extern unsigned char CRC16_High, CRC16_Low;

void CRC16_Calc( void *buffer,uint8_t len);
void CRC16_CalcBlock(void *Block,int low_endian = 0);
void CRC16_CalcSector(void *Sector,int low_endian = 0);
void CRC16_CalcData( unsigned char *data,unsigned char datalen);

unsigned char CRC16_CheckSector(unsigned char *Block0,unsigned char *Block1,unsigned char *Block2);
unsigned char CRC16_CheckBlock(unsigned char *Block);

uint8_t CheckDataCRC16(void *data, uint8_t DataSize,int low_endian = 0);

#endif// __CRC16_H__

