#include "export.h"
#include "terminal_protocol.h"

template<size_t Timeout, uint8_t Type = FMAS>
class CustomTerminalProtocol : public TerminalProtocol
{
public:
	CustomTerminalProtocol(IOProvider *provider):TerminalProtocol(provider) {
		set_timeout(Timeout);
		set_type(Type);
	}
};

struct terminal_entries_get
{
	uint8_t dts11          :1;
	uint8_t dts12          :1;
	uint8_t key1           :1;
	uint8_t in1            :1;
	uint8_t in2            :1;
	uint8_t key2           :1;
	uint8_t dst22          :1;
	uint8_t dts21          :1;

	uint8_t stp_time       :1;
	uint8_t stp_mes        :1;
	uint8_t stp_tarif      :1;
	uint8_t stp_places     :1;
	uint8_t stp_paper_near :1;
	uint8_t stp_paper_no   :1;
	uint8_t stp_reserved   :2;

	uint8_t in_count;
	uint8_t out_count;
	uint8_t status_reason;
};

EXPORT long terminal_get_entries(Reader *reader, uint8_t addr, terminal_entries_get *entries) {	
	return reader->send_command<TerminalProtocol>(addr,'G',entries);
}

EXPORT long terminal_reset_entries(Reader *reader, uint8_t addr) {
	return reader->send_command<CustomTerminalProtocol<0,FACK> >(addr,'G');
}

struct terminal_entries_set
{
	uint8_t reason;
	uint8_t command;
};

EXPORT long terminal_set_state(Reader *reader, uint8_t addr, terminal_entries_set *entries) {
	return reader->send_command<TerminalProtocol>(addr,'S',entries,(uint8_t*)0);
}

//warning: terminal will return big-endian structure here
struct terminal_counters
{
	uint16_t free_places;
};

EXPORT long terminal_set_counters(Reader *reader, uint8_t addr, terminal_counters *counters) {
	return reader->send_command<CustomTerminalProtocol<0> >(addr,'C',counters,(uint8_t*)0);
}

struct terminal_reader
{
	uint8_t time;
	uint8_t status;
	uint8_t card[10];
};

struct terminal_readers
{
	terminal_reader in;
	terminal_reader out;
};

EXPORT long terminal_get_readers(Reader *reader, uint8_t addr, terminal_readers *rdrs) {
	return reader->send_command<TerminalProtocol>(addr,'R',rdrs);
}

EXPORT long terminal_ack_readers(Reader *reader, uint8_t addr) {
	return reader->send_command<CustomTerminalProtocol<0,FACK> >(addr,'R');
}

struct terminal_barcode
{
	uint8_t time;
	uint8_t status;
	uint8_t code[18];
};

EXPORT long terminal_get_barcode(Reader *reader, uint8_t addr, terminal_barcode *barcode) {
	return reader->send_command<TerminalProtocol>(addr,'B',barcode);
}

EXPORT long terminal_ack_barcode(Reader *reader, uint8_t addr) {
	return reader->send_command<CustomTerminalProtocol<0,FACK> >(addr,'B');
}

struct terminal_strings
{
	char tariff_names[8][20];
	char check_lines[4][10];
};

EXPORT long terminal_set_strings(Reader *reader, uint8_t addr, terminal_strings *strings) {
	return reader->send_command<CustomTerminalProtocol<0> >(addr,'M',strings,(uint8_t*)0);
}

struct terminal_time
{
	uint8_t year;   // 0-99
	uint8_t month;  // 1-12
	uint8_t day;    // 1-31
	uint8_t hour;   // 0-23
	uint8_t minute; // 0-59
	uint8_t second; // 0-59
};

EXPORT long terminal_set_time(Reader *reader, uint8_t addr, terminal_time *t) {
	return reader->send_command<CustomTerminalProtocol<0> >(addr,'T',t,(uint8_t*)0);
}

