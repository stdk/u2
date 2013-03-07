#ifndef COMMANDS_H
#define COMMANDS_H

#define ERROR_READ            8
#define ERROR_WRITE           9
#define ERROR_VALUE          11
#define NO_COMMAND          254
#define CRC_ERROR           255

//reader
#define GET_SN               0x10
#define GET_VERSION          0x02 
#define FIELD_ON             0x4E
#define FIELD_OFF            0x4F
#define MULTIBYTE_PACKAGE	 0x04
#define SYNC_WITH_DEVICE	 0x05
#define UPDATE_START         0x06

//card
#define ANTICOLLISION        0x22
#define REQUEST_STD          0x40
#define SELECT               0x43
#define AUTH                 0x44
#define AUTH_DYN             0xBB
#define BLOCK_READ           0xBC
#define BLOCK_WRITE          0xBD
#define SECTOR_READ          0xBE
#define SECTOR_WRITE         0xBF
#define SET_TRAILER          0xC0
#define SET_TRAILER_DYN      0xC1

#endif