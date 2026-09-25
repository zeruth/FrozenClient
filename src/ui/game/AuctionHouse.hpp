#ifndef UI_GAME_AUCTION_HOUSE_HPP
#define UI_GAME_AUCTION_HOUSE_HPP

#include "util/guid/Types.hpp"
#include <storm/Array.hpp>
#include <cstdint>

// One auction as the auction lists hold it, 0xa0 bytes in the reference. The fields are not
// recovered: the list handlers that fill it are not ported.
struct AUCTIONITEM {
    uint32_t unk00[0x28];
};

class AuctionHouse {
    public:
        // Public static variables
        static TSGrowableArray<AUCTIONITEM> s_listItems;    // ref: DAT_00c0f440
        static TSGrowableArray<AUCTIONITEM> s_ownerItems;   // ref: DAT_00c0f450
        static TSGrowableArray<AUCTIONITEM> s_bidderItems;  // ref: DAT_00c0f460
        static WOWGUID s_sellItem;                          // ref: DAT_00c0f3f8

        // Public static functions
        static AUCTIONITEM* GetBidderItem(uint32_t index);
        static AUCTIONITEM* GetListItem(uint32_t index);
};

#endif
