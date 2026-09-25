#include "object/client/ItemLink.hpp"

#include "object/client/CGItem_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/ItemCache.hpp"

#include <storm/String.hpp>

namespace {

// ref: PTR_s__cff9d9d9d_00ad2a84
// Indexed by quality. Artifact and heirloom share one string in the reference -- the same pointer
// appears twice -- so they are not distinct colours despite being distinct qualities.
const char* s_qualityColours[8] = {
    "|cff9d9d9d", // poor
    "|cffffffff", // common
    "|cff1eff00", // uncommon
    "|cff0070dd", // rare
    "|cffa335ee", // epic
    "|cffff8000", // legendary
    "|cffe6cc80", // artifact
    "|cffe6cc80", // heirloom
};

// The reference formats into one shared 1024-byte buffer and returns a pointer to it, so every
// caller has to consume the link before building another. Kept, rather than returned by value,
// because callers push it straight to Lua and the lifetime question never arises.
char s_link[1024];
char s_name[256];

} // namespace

// ref: FUN_00706d70
const char* ItemNameFromEntry(int32_t entryID, int32_t suffix) {
    s_name[0] = '\0';

    auto info = ItemCacheGet(entryID);

    if (!info || !info->Name() || !info->Name()[0]) {
        return s_name;
    }

    // TODO the suffix decoration. A positive suffix names an ItemRandomProperties row and a
    // negative one an ItemRandomSuffix row, and the two are combined with the item's own name
    // through the ITEM_SUFFIX_TEMPLATE global string. Frozen has neither DBC, so a decorated item
    // reports its plain name -- right for every item without a random property, which is most of
    // them, and short by the suffix for the rest.
    SStrCopy(s_name, info->Name(), sizeof(s_name));

    return s_name;
}

// ref: FUN_0061e290
const char* ItemLinkBuild(int32_t entryID, int32_t quality, int32_t enchant,
                          const ITEM_LINK_GEMS& gems, int32_t suffix, int32_t seed) {
    // The player's level is baked into every link. It is what lets a recipient's client decide
    // whether to show the item as usable, and it is the LINKING player's level, not the reader's.
    auto player = CGPlayer_C::GetActivePtr();
    auto unit = player ? player->Unit() : nullptr;
    auto level = unit ? unit->level : 0;

    // Out of range clamps to 1, common white -- not to 0, which would be poor grey.
    auto colour = s_qualityColours[(quality < 0 || quality > 7) ? 1 : quality];

    // The trailing reset is dropped when the colour is empty, so a link never carries a stray |r.
    auto reset = colour[0] ? "|r" : "";

    // Nine numeric fields. The seventh is always zero: a fourth gem socket that nothing fills.
    SStrPrintf(
        s_link, sizeof(s_link),
        "%s|Hitem:%d:%d:%d:%d:%d:%d:%d:%d:%d|h[%s]|h%s",
        colour, entryID, enchant, gems.gem[0], gems.gem[1], gems.gem[2], 0, suffix, seed, level,
        ItemNameFromEntry(entryID, suffix), reset
    );

    return s_link;
}

// ref: FUN_0061e360
const char* ItemLinkBuild(int32_t entryID, int32_t quality, int32_t enchant, int32_t suffix, int32_t seed) {
    ITEM_LINK_GEMS gems;

    return ItemLinkBuild(entryID, quality, enchant, gems, suffix, seed);
}

// ref: FUN_0061e3a0
const char* ItemLinkFromObject(CGItem_C* item) {
    if (!item) {
        return nullptr;
    }

    auto data = item->Item();

    if (!data) {
        return nullptr;
    }

    auto info = ItemCacheGet(item->GetEntryID());

    // An item whose cache record has not arrived still links, as common quality. The reference
    // uses 1 rather than waiting or failing.
    auto quality = info ? info->quality : 1;

    ITEM_LINK_GEMS gems;

    for (int32_t i = 0; i < 3; i++) {
        gems.gem[i] = data->enchantments[i].id;
    }

    return ItemLinkBuild(
        item->GetEntryID(),
        quality,
        data->enchantments[0].id,
        gems,
        data->randomPropertiesID,
        data->propertySeed
    );
}
