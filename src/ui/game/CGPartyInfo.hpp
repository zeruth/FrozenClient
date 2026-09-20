#ifndef UI_GAME_C_G_PARTY_INFO_HPP
#define UI_GAME_C_G_PARTY_INFO_HPP

#include "util/GUID.hpp"

class CGPartyInfo {
    public:
        // Public static functions
        static uint32_t NumMembers();

        // One member by 1-based index, as the party1..party4 unit tokens number them. Returns 0
        // for an empty slot or an index outside 1..4.
        //
        // NOTE the roster is never populated yet -- see GetMember's definition -- so this answers
        // 0 for everything today.
        static WOWGUID GetMember(uint32_t index);

    private:
        // Private static variables
        static WOWGUID m_members[];
};

#endif
