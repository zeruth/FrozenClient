#ifndef UI_ADD_ON_HPP
#define UI_ADD_ON_HPP

#include <cstdint>

// Loads the Blizzard interface add-ons that FrameXML defers rather than loading itself.
//
// Five of them (combat text, the time manager, the combat log, the token UI, the knowledge base)
// hold roughly 130 of the interface's script functions. FrameXML only defines the one-line
// <name>_LoadUI stubs that call UIParentLoadAddOn, so with no loader behind LoadAddOn those
// functions simply never come into existence -- silently, because nothing fails.

// Whether an add-on has already been loaded this session.
bool AddOnIsLoaded(const char* name);

// Loads Interface\AddOns\<name>\<name>.toc. Returns 1 on success, or 0 with `reason` set to the
// token the interface expects ("MISSING" when the add-on is not in the archives).
int32_t AddOnLoad(const char* name, const char** reason);

// The "checkAddonVersion" CVar: whether an add-on built for an older interface is refused.
bool AddOnVersionCheckEnabled();
void AddOnSetVersionCheck(bool enabled);

#endif
