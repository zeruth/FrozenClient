#ifndef OBJECT_CLIENT_UNIT_VISUALS_HPP
#define OBJECT_CLIENT_UNIT_VISUALS_HPP

#include "util/guid/Types.hpp"
#include <cstdint>

class CGUnit_C;

// Spell visuals that stay on a unit: the STATE kit of every aura it carries.
//
// An aura's spell names a SpellVisual; its StateKit is a SpellVisualKit that names up to eleven
// SpellVisualEffectName models, one per attachment slot -- head, chest, base (the ground under the
// unit), hands, breath, weapons and three "special" points. Each is an M2 attached to the unit's
// model at that point, so it follows the unit and animates with the scene. The swirl under the Lich
// King is the base effect of his aura's state kit.
//
// UnitVisualsUpdate diffs the unit's aura set (AuraCache) against what is attached and creates or
// releases effect models to match; it is cheap when nothing changed. UnitVisualsRelease drops
// everything for a unit that is going away.

void UnitVisualsUpdate(CGUnit_C* unit);

void UnitVisualsRelease(WOWGUID guid);

void UnitVisualsReleaseAll();

#endif
