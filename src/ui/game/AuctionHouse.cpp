#include "ui/game/AuctionHouse.hpp"

TSGrowableArray<AUCTIONITEM> AuctionHouse::s_listItems;
TSGrowableArray<AUCTIONITEM> AuctionHouse::s_ownerItems;
TSGrowableArray<AUCTIONITEM> AuctionHouse::s_bidderItems;
WOWGUID AuctionHouse::s_sellItem;

// ref: FUN_0059b2c0
AUCTIONITEM* AuctionHouse::GetBidderItem(uint32_t index) {
    if (index < AuctionHouse::s_bidderItems.Count()) {
        return &AuctionHouse::s_bidderItems.m_data[index];
    }

    return nullptr;
}

// ref: FUN_0059b280
AUCTIONITEM* AuctionHouse::GetListItem(uint32_t index) {
    if (index < AuctionHouse::s_listItems.Count()) {
        return &AuctionHouse::s_listItems.m_data[index];
    }

    return nullptr;
}
