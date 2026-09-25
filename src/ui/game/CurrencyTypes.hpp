#ifndef UI_GAME_CURRENCY_TYPES_HPP
#define UI_GAME_CURRENCY_TYPES_HPP

#include <cstdint>

class CurrencyTypesRec;

// One row of the currency list the token frame shows, 0x18 bytes in the reference. The fields
// are not recovered: the builder that fills the list is not ported.
struct CURRENCYLISTENTRY {
    uint32_t unk00[6];
};

CurrencyTypesRec* CurrencyGetBackpackCurrency(int32_t index);

CURRENCYLISTENTRY* CurrencyGetListEntry(int32_t index);

#endif
