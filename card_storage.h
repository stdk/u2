#ifndef CARD_STORAGE
#define CARD_STORAGE

#include "api_common.h"

struct SectorStorage : public Sector
{
	 enum auth_status {
		NO_AUTH = 0,
		AUTHENTICATED = 1
	};

	uint8_t enc[3]; // encryption keys for every block, reading or writing whole sector assumes enc[0] 
	uint8_t status; // 0 - no authentication, 1 - authenticated
	uint8_t pad[9];

	SectorStorage(uint8_t num = 0, uint8_t key = 0, uint8_t mode = STATIC);
};

struct CardStorage
{
	uint64_t sn;
	uint64_t unused;
	SectorStorage sectors[16];

	uint64_t generate_random_sn();

	CardStorage(const char *path = 0);

	long load(const char *path);
	long save(const char *path);
};

#endif