#include "export.h"
#include "terminal_protocol.h"

struct stoppark_entries_get
{
	uint8_t DTS11        :1;
	uint8_t DTS12        :1;
	uint8_t Key1         :1;
	uint8_t In1          :1;
	uint8_t In2          :1;
	uint8_t Key2         :1;
	uint8_t DTS22        :1;
	uint8_t DTS21        :1;

	uint8_t STPTime      :1;
	uint8_t STPMes       :1;
	uint8_t STPTarif     :1;
	uint8_t STPPlaces    :1;
	uint8_t STPPaperNear :1;
	uint8_t STPPaperNo   :1;
	uint8_t STPReserved  :2;

	uint8_t InCount;
	uint8_t OutCount;
	uint8_t StatusReason;
};

EXPORT long terminal_read_entries(Reader *reader, uint8_t addr, stoppark_entries_get *entries)
{	
	return reader->send_command<TerminalProtocol>(addr,'G',entries);
}
