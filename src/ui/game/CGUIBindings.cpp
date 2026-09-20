#include "ui/game/CGUIBindings.hpp"

#include "event/Event.hpp"
#include "event/Types.hpp"

#include <common/DataStore.hpp>
#include <common/xml/XMLNode.hpp>
#include <common/xml/XMLTree.hpp>
#include <storm/String.hpp>

TSGrowableArray<MODIFIED_CLICK> CGUIBindings::s_modifiedClicks;

namespace {

// Longest first: LSHIFT has to be tested before SHIFT or the prefix compare would match SHIFT
// against "LSHIFT" and consume the wrong number of characters.
struct MODIFIER_TOKEN {
    const char* text;
    size_t length;
    uint32_t flag;
};

const MODIFIER_TOKEN s_modifierTokens[] = {
    { "LSHIFT", 6, MODIFIER_LSHIFT },
    { "RSHIFT", 6, MODIFIER_RSHIFT },
    { "SHIFT",  5, MODIFIER_SHIFT  },
    { "LCTRL",  5, MODIFIER_LCTRL  },
    { "RCTRL",  5, MODIFIER_RCTRL  },
    { "CTRL",   4, MODIFIER_CTRL   },
    { "LALT",   4, MODIFIER_LALT   },
    { "RALT",   4, MODIFIER_RALT   },
    { "ALT",    3, MODIFIER_ALT    },
};

// The six modifier keys, indexed by the bit position of their flag.
const KEY s_modifierKeys[] = {
    KEY_LSHIFT,
    KEY_RSHIFT,
    KEY_LCONTROL,
    KEY_RCONTROL,
    KEY_LALT,
    KEY_RALT,
};

} // namespace

// ref: FUN_0055d860
// Walks a modifier expression from the front, OR-ing in one flag per token and stepping over a
// single "-" between them. It stops at the first token it does not recognise rather than failing,
// which is what makes "SHIFT-BUTTON1" parse as plain shift: the button half is for the click
// handler, not for the modifier test.
//
// The caller's pointer is advanced past what was consumed, so a caller can tell a pure modifier
// expression from an action name by whether anything was parsed at all.
uint32_t CGUIBindings::ParseModifierFlags(const char** text) {
    uint32_t flags = 0;

    while (**text) {
        auto matched = false;

        for (auto& token : s_modifierTokens) {
            if (!SStrCmpI(*text, token.text, token.length)) {
                flags |= token.flag;
                *text += token.length;
                matched = true;

                break;
            }
        }

        if (!matched) {
            break;
        }

        if (**text == '-') {
            (*text)++;
        }
    }

    return flags;
}

// A modifier group is satisfied when any one of the keys it names is held, and a group the
// expression says nothing about is not tested at all -- so "SHIFT" is true with shift and control
// both down. The reference writes this out per group with the pairs unrolled; same predicate.
bool CGUIBindings::ModifierFlagsHeld(uint32_t flags) {
    static const uint32_t groups[] = { MODIFIER_SHIFT, MODIFIER_CTRL, MODIFIER_ALT };

    for (auto group : groups) {
        auto required = flags & group;

        if (!required) {
            continue;
        }

        auto held = false;

        for (uint32_t bit = 0; bit < 6; bit++) {
            if ((required & (1u << bit)) && EventIsKeyDown(s_modifierKeys[bit])) {
                held = true;

                break;
            }
        }

        if (!held) {
            return false;
        }
    }

    return true;
}

// A whole binding is a modifier expression, an optional BUTTONn, or the word NONE -- and nothing
// left over. This is what SetModifiedClick validates against before it will store anything.
//
// The button is parsed but goes unused by the modifier test: it is the click that has to match it,
// not the keyboard. It is kept so the string round-trips, which is how the reference rebuilds the
// text it hands back from GetModifiedClick.
bool CGUIBindings::ParseBinding(const char* text, uint32_t* flags, uint32_t* button) {
    *flags = 0;
    *button = 0;

    if (!text || !*text) {
        return false;
    }

    if (!SStrCmpI(text, "NONE", STORM_MAX_STR)) {
        return true;
    }

    auto cursor = text;
    *flags = CGUIBindings::ParseModifierFlags(&cursor);

    if (!*cursor) {
        return *flags != 0;
    }

    if (SStrCmpI(cursor, "BUTTON", 6)) {
        return false;
    }

    cursor += 6;

    uint32_t index = 0;

    while (*cursor >= '0' && *cursor <= '9') {
        index = index * 10 + (*cursor - '0');
        cursor++;
    }

    if (*cursor || !index) {
        return false;
    }

    *button = index;

    return true;
}

MODIFIED_CLICK* CGUIBindings::GetModifiedClick(const char* action) {
    for (uint32_t i = 0; i < CGUIBindings::s_modifiedClicks.Count(); i++) {
        auto& click = CGUIBindings::s_modifiedClicks[i];

        if (!SStrCmpI(click.action.GetString(), action, STORM_MAX_STR)) {
            return &click;
        }
    }

    return nullptr;
}

// ref: FUN_0055f940
// The argument is tried as a modifier expression FIRST, so IsModifiedClick("SHIFT") answers
// whether shift is held without anything having been registered. Only a string that parses to
// nothing is looked up as a declared action -- which is why an action can never be named "ALT".
bool CGUIBindings::IsModifiedClick(const char* action) {
    auto text = action;
    auto flags = CGUIBindings::ParseModifierFlags(&text);

    if (flags) {
        return CGUIBindings::ModifierFlagsHeld(flags);
    }

    auto click = CGUIBindings::GetModifiedClick(action);

    if (!click) {
        return false;
    }

    auto modifier = click->modifier.GetString();

    return CGUIBindings::ModifierFlagsHeld(CGUIBindings::ParseModifierFlags(&modifier));
}

bool CGUIBindings::AnyModifierHeld() {
    for (auto key : s_modifierKeys) {
        if (EventIsKeyDown(key)) {
            return true;
        }
    }

    // TODO the reference then walks a chain off the bindings object (+0x100/+0x108) before giving
    // up. What that chain holds is not identified yet; with no modifier held it answers false here.
    return false;
}

// Part of what the reference's CGUIBindings::Load does. Bindings.xml carries <Binding> elements as
// well, and those are NOT handled here -- key bindings need the binding manager, which is not
// ported. Only the <ModifiedClick> declarations are read, so the five modified-click bindings work
// while GetBinding and friends stay stubs.
void CGUIBindings::LoadModifiedClicks(const char* filePath, CStatus* status) {
    auto tree = FrameXML_LoadXML(filePath, nullptr, status);

    if (!tree) {
        return;
    }

    auto root = XMLTree_GetRoot(tree);

    for (auto node = root ? root->GetChild() : nullptr; node; node = node->GetSibling()) {
        if (SStrCmpI(node->GetName(), "ModifiedClick", STORM_MAX_STR)) {
            continue;
        }

        auto action = node->GetAttributeByName("action");

        if (!action || !*action) {
            continue;
        }

        auto defaultModifier = node->GetAttributeByName("default");

        if (!defaultModifier) {
            defaultModifier = "";
        }

        auto click = CGUIBindings::s_modifiedClicks.New();
        click->action.Copy(action);
        click->defaultModifier.Copy(defaultModifier);
        click->modifier.Copy(defaultModifier);
    }

    XMLTree_Free(tree);
}
