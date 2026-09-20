#ifndef UI_GAME_UI_BINDINGS_HPP
#define UI_GAME_UI_BINDINGS_HPP

#include <cstdint>
#include <string>

// The key binding commands, as declared by Interface\FrameXML\Bindings.xml.
//
// A "binding" here is a COMMAND -- MOVEFORWARD, ACTIONBUTTON1, TOGGLEGAMEMENU -- together with
// whatever keys are currently bound to it. The command list is fixed and comes from the XML; the
// keys are per-profile and arrive from a saved bindings cache. This client has no such cache yet,
// so every command is declared and none is bound, which is exactly the state the reference is in
// on a profile that has never saved one: the key binding pane lists every command as "Not Bound".
//
// See docs/ref/parity-bindings.md for where the pieces live in the reference.
struct UIBindingCommand {
    // The command name FrameXML asks for by string ("ACTIONBUTTON1").
    std::string name;

    // The group the key binding pane files it under ("MOVEMENT", "ACTIONBAR"). Only the first
    // command of a group carries one in the XML; the rest inherit it, and that inheritance is
    // resolved at load so every command here knows its own header.
    std::string header;

    // The Lua the command runs. Kept because the dispatch half will need it; nothing reads it yet.
    std::string script;

    // The command fires on key release as well as press.
    bool runOnUp = false;

    // A section title rather than a real command, named HEADER_<group>. The reference registers
    // these in the command list so the key binding pane finds its headings by walking indices.
    bool isHeader = false;

    // Registered, and bindable by name, but left out of the numbered walk the pane does.
    bool hidden = false;

    // Up to two keys may be bound to one command. Empty means not bound.
    std::string keys[2];
};

int32_t UIBindingsGetCount();

// By position in the XML, which is the order the key binding pane walks. Null when out of range.
const UIBindingCommand* UIBindingsGetByIndex(int32_t index);

const UIBindingCommand* UIBindingsFind(const char* name);

// The nth key bound to a command, or null when there is no nth key. Callers walk n upward until
// they get null, which is how the reference's own binding getters are shaped.
const char* UIBindingsGetKey(const char* command, int32_t n);

// The command a key is bound to, or null.
const char* UIBindingsGetCommandForKey(const char* key);

// Bind a key to a command, or unbind it when command is null or empty. Takes the key off whatever
// held it first, since a key maps to at most one command. False when the command does not exist.
bool UIBindingsSetKey(const char* key, const char* command);

#endif
