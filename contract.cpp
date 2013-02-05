#include "api_internal.h"
#include "api_common.h"
#include "protocol.h"

#include <iostream>

#pragma pack(push,1)
struct PURSEDATA
{
	uint8_t version;
	uint16_t end;
	uint8_t status;
	uint32_t value;
	uint16_t start;
	uint16_t transactions;
};
#pragma pack(pop)

API_FUNCTION_1_TO_1(0x82,card_read_purse,SN5,PURSEDATA)

#pragma pack(push,1)

typedef struct _MFSTD1K_CONTRACT_STATIC302H
{
  union
  {
    struct
    {
      uint8_t Identifier;
      uint8_t VersionNumber                 :6;
      uint8_t BitMap                        :2;
      uint32_t AID                          :12;
      uint32_t PIX                          :12;
      uint32_t ContractStatus               :8;
      uint64_t ContractPriority              :2;
      uint64_t ContractSerialNumber          :32;
      uint64_t SaleAID                       :12;
      uint64_t Place                         :12;
      uint64_t DeviceNumber                  :6;
      uint8_t DeviceType                    :4;
      uint8_t ContractDataPointer           :4;
      uint8_t TransportType                 :8;
    } ContractFields;
    uint8_t data[16];
  } Block0;

  union
  {
    struct
    {
      uint64_t   RestrictDOW                   :8;
      uint64_t   RestrictTimeCode              :8;
      uint64_t   ContractDoubleUseDuration     :10;
      uint64_t   ContractDoubleUseDurationUnit :4;
      uint64_t   ContractMaxValue              :24;
      uint64_t   PeriodJorneys                 :10;
      uint64_t   PeriodJorneysMax              :10;
      uint64_t   ValiditionModel               :2;
      uint64_t   ValidityStartDate             :14;
      uint64_t   ValidityEndDate               :14;
      uint64_t   ValidityLimitDate             :14;
      uint64_t   reserved1                     :10;
    } ContractFields;
    uint8_t data[16];
  } Block1;

  union
  {
    struct
    {
      uint16_t AutoLoadStatus                :2;
      uint16_t ValidityDurationUnit          :4;
      uint16_t ValidityDuration              :10;
      uint64_t   ValidityDurationLastUse     :10;
      uint64_t   MinimumValue                :24;
      uint64_t   AutoLoadValue               :24;
      uint64_t   reserved2                   :6;
      uint32_t MinimumJorney                 :10;
      uint32_t AutoLoadJorney                :10;
      uint32_t MaxAutoLoadPeriod             :4;
      uint32_t MACAlgorithmIdentifier        :2;
      uint32_t MACKeyIdentifier              :6;
      uint16_t  MACAuthenticator16;
    } ContractFields;
    uint8_t data[16];
  } Block2;
} MFSTD1KContractStatic302h;

#pragma pack(pop)

EXPORT void term_set_validity(MFSTD1KContractStatic302h* term, uint16_t start,uint16_t end)
{
	term->Block1.ContractFields.ValidityStartDate = start;
	term->Block1.ContractFields.ValidityEndDate   = end;
}

struct term_init_args
{
	uint16_t status;
	uint16_t aid;
	uint16_t pix;
	uint16_t start;
	uint16_t end;
	uint16_t limit;
};

EXPORT void term_init(MFSTD1KContractStatic302h* term, term_init_args* init)
{
	term->Block0.ContractFields.Identifier = 0x89;
	term->Block0.ContractFields.AID = init->aid;
	term->Block0.ContractFields.PIX = init->pix;
	term->Block0.ContractFields.SaleAID = 0xD01;
	term->Block0.ContractFields.ContractDataPointer = 14;
	term->Block0.ContractFields.ContractStatus = init->status;
	term->Block0.ContractFields.TransportType = (init->pix >> 3) & 0x1F; // select 6 bits from PIX
	term->Block1.ContractFields.ValiditionModel = 2;
	term->Block1.ContractFields.ContractDoubleUseDuration = 15;
	term->Block1.ContractFields.ContractDoubleUseDurationUnit = 1;
	term->Block1.ContractFields.ValidityStartDate = init->start;
	term->Block1.ContractFields.ValidityEndDate = init->end;
	term->Block1.ContractFields.ValidityLimitDate = init->limit;	
}

#pragma pack (push,1)
struct AIDPIX
{
	uint16_t AID;
	uint16_t PIX;
};
#pragma pack (pop)

#pragma pack (push,1)
struct IN_TCULIGHTREAD
{
	uint8_t  CardVersion;
	uint8_t  BitMap;
	uint16_t AID;
	uint16_t PIX;
	uint8_t  CardSystemNumber[10];
	uint32_t ContractSerialNumber;
	uint8_t  ContractDoubleUseStatus;
	uint8_t  TransportType;
	uint8_t  ValidityYear;
	uint8_t  ValidityMonth;
	//uint8_t  PeriodJourneys;
	
	uint16_t ValidationLastDate;
	uint16_t ValidationLastTime;
	uint16_t PlaceID;
	uint8_t  DeviceType;
	uint8_t  DeviceNumber;
	uint16_t ContractTransactionNumberMetro;
	uint8_t  IsActiveContract;
};
#pragma pack (pop)

API_FUNCTION_1_TO_1(0xB3,card_read_ulight,AIDPIX,IN_TCULIGHTREAD);

#pragma pack (push,1)
struct IN_TCULIGHTACTIV
{
	AIDPIX aidpix;

	uint16_t ValidationLastDate;
	uint16_t ValidationLastTime;
	uint16_t PlaceID;
	uint8_t DeviceType;
	uint8_t DeviceNumber;
};
#pragma pack (pop)

API_FUNCTION_1_TO_0(0xB2,card_activate_ulight,IN_TCULIGHTACTIV);