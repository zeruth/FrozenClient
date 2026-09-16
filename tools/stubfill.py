#!/usr/bin/env python3
"""Bulk-implement stubbed bindings whose correct answer follows from a rule.

Hand-picking does not scale against ~2000 stubs. This fills whole classes at once, but only the two
where the answer is determined rather than guessed:

  number   FrameXML does arithmetic on the result, or loops to it. nil there is a HARD ERROR --
           every "attempt to perform arithmetic on a nil value" in the run logs is one of these.
           The subsystem behind them is absent, so the count/amount is genuinely 0.

  boolean  FrameXML branches on the result. nil already reads as false, so the value does not
           change, but returning a real false stops the stub warning and states the answer.
           Applied ONLY to names that are actually predicates -- Is*, Can*, Has*, In*, and the
           *Is*/*Has* forms. A Get* that happens to be used in a condition returns a string or a
           number, and answering false there would be a new bug.

Anything whose shape is 'value' (destructured by the caller) is left alone: arity matters there and
it has to be read off the call site one at a time.

    python tools/stubfill.py --dry-run
    python tools/stubfill.py --apply
"""

import glob
import importlib.util
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FXDIR = os.path.join(ROOT, 'build', 'framexml')

# A predicate by name. Get*/Set* are excluded deliberately, even when used in a condition.
PREDICATE = re.compile(r'^(Is|Can|Has|In|Are|Should|Does)[A-Z]')
PREDICATE_INFIX = re.compile(r'(Is|Has|Can)[A-Z]')

# Bindings the rules would mishandle. Each is a real function or returns a non-boolean.
EXCLUDE = {
    # return strings or lists
    'GetLocale', 'GetExistingLocales', 'GetBindingAction', 'GetPartyAssignment', 'GetItemSpell',
    'GetPetIcon', 'GetGuildInfoText', 'GetAbandonQuestName', 'GetQuestLogCompletionText',
    'GetMultiCastTotemSpells', 'GetEquipmentSetInfoByName', 'GetArenaTeamRosterSelection',
    'GetGuildRosterSelection', 'GetContainerFreeSlots', 'ContainerIDToInventoryID',
    'GetRealNumRaidMembers', 'GetRealNumPartyMembers', 'GetPetActionSlotUsable',
    # real utilities that should compute something, not answer 0
    'strlenutf8', 'GetNumAddOns',
    # input state the client genuinely has; answering false would be wrong, not merely incomplete
    'IsMouseButtonDown',
    # actions, not predicates -- a return value is meaningless but a stub warning is honest
    'ForceLogout', 'InteractUnit', 'ClearTarget', 'SpellStopCasting', 'SpellStopTargeting',
    'PutItemInBackpack', 'SetSendMailMoney', 'SortQuestWatches', 'AcceptBattlefieldPort',
    'ClickStablePet', 'GuildRoster', 'QueryGuildEventLog', 'FrameXML_Debug', 'CompleteLFGRoleCheck',
    'BNCreateConversation', 'BNSetMatureLanguageFilter',
}

NUMBER_BODY = """    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;"""

BOOLEAN_BODY = """    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;"""


def load_triage():
    spec = importlib.util.spec_from_file_location('st', os.path.join(ROOT, 'tools', 'stubtriage.py'))
    st = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(st)

    stubs = st.collect_stubs()
    usage = st.scan_framexml(FXDIR)

    out = {}

    for name, (path, kind) in stubs.items():
        out[name] = (path, kind, st.shape_of(usage.get(name)))

    return out


def wanted(name, shape):
    if name in EXCLUDE:
        return None

    if shape == 'number':
        return NUMBER_BODY

    if shape == 'boolean':
        if PREDICATE.match(name) or PREDICATE_INFIX.search(name):
            if not name.startswith(('Get', 'Set')):
                return BOOLEAN_BODY

    return None


def main():
    apply = '--apply' in sys.argv
    triage = load_triage()

    # name -> body, grouped by the file that defines it.
    edits = {}

    for name, (path, kind, shape) in triage.items():
        body = wanted(name, shape)

        if body:
            edits.setdefault(path, []).append((name, kind, body))

    done = 0
    skipped = []

    for path, items in sorted(edits.items()):
        full = os.path.join(ROOT, path)
        src = io.open(full, encoding='utf-8', errors='replace').read()
        before = src

        # Map registered Lua name -> C++ function name for this file.
        reg = dict((n, f) for n, f in re.findall(r'\{\s*"([^"]+)"\s*,\s*&(\w+)\s*\}', src))

        for name, kind, body in items:
            if kind == 'action':
                old = 'WHOA_LUA_STUB(%s)' % name
                new = 'int32_t Script_Stub_%s(lua_State* L) {\n%s\n}' % (name, body)
            else:
                func = reg.get(name)

                if not func:
                    skipped.append(name)
                    continue

                old = 'int32_t %s(lua_State* L) {\n    WHOA_UNIMPLEMENTED(0);\n}' % func
                new = 'int32_t %s(lua_State* L) {\n%s\n}' % (func, body)

            if src.count(old) != 1:
                skipped.append(name)
                continue

            src = src.replace(old, new)
            done += 1

        if src != before and apply:
            io.open(full, 'w', encoding='utf-8', newline='\n').write(src)

    print('%s %d bindings across %d files (%d skipped)'
          % ('filled' if apply else 'would fill', done, len(edits), len(skipped)))

    if skipped:
        print('skipped:', ', '.join(sorted(set(skipped))[:12]))


if __name__ == '__main__':
    main()
