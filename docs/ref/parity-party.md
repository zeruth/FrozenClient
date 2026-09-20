# Parity: the party roster

Frozen's party is permanently empty. `CGPartyInfo::m_members` is declared, counted, and never
written, because the message that fills it is not handled. This is a short doc because the work is
not done — it exists so the next attempt does not repeat the hunt.

## Why it matters more than it looks

The roster is not just the party frames. It is the backing for the `party1`-`party4` and
`partypet1`-`partypet4` unit tokens, and those are what `Script_GetTokenFromGUID` walks. With the
roster empty that function can only ever answer `player`, `target`, `pet` or `focus`, so:

- `UNIT_AURA`, the seven `UNIT_SPELLCAST_*` events, `UNIT_NAME_UPDATE` and `MINIMAP_PING` all fire
  for the player and for nobody else in the group. Each of those handlers is correct; they simply
  have no token to name a group member with.
- `UnitIsFeignDeath` and `UnitPlayerOrPetInParty` cannot be answered at all. Both look like flag
  tests — and the flags are present — but each gates on party-or-raid membership.
- Every party frame has nothing to draw.

## The handler

`SMSG_GROUP_LIST` is **`0x7D`**, already in frozen's enum, and the reference's handler is
**`FUN_006d8870`**.

Finding it is worth recording, because two obvious routes both failed. The party module's own init
(`FUN_0052d0e0`, which zeroes the roster) registers only the two difficulty messages. And scanning
the image for direct stores to the roster array `DAT_00bd1948` finds *only* that zeroing, because
the per-member writes go through a computed address.

What worked: scan `.text` for every call to `SetMessageHandler` (`FUN_006b0b80`), decode the
`push` immediates in the two dozen bytes before each call, and keep the sites that push `0x7d`.
There are 598 registration sites and exactly one match, giving the handler pointer directly. The
same trick will find any other unhandled opcode's reference implementation.

## State the reference keeps

| global | meaning |
|---|---|
| `DAT_00bd1948` | member guid array, 4 entries of 8 bytes |
| `DAT_00bd0e68` | member pet guids |
| `DAT_00bd0d08` | per-member record block, copied wholesale (0xc4 dwords) when the roster is rebuilt |
| `DAT_00bd1968` / `DAT_00bd196c` | the **leader** guid -- `IsPartyLeader` compares it against the active player, so it is not the group guid as first recorded |
| `DAT_00bd198a` | loot method or difficulty byte |

## Wire format, as far as it is read

Reading `FUN_006d8870`, with `Get8`/`Get32`/`Get64` for the three CDataStore readers:

```
u8  groupType
u8  subGroup
u8  flags
u8  roles
if (groupType & 0x08) { u8 lfgState; u32 lfgFlags; }     // the LFG block is conditional
u64 groupGuid
u32 counter                                              // a sequence number, see below
u32 memberCount
... memberCount member records ...
```

`groupType` is a bitmask, recovered from how the handler branches on it:

| bit | meaning | what it changes |
|---|---|---|
| `0x01` | battleground | which of the two duplicate-detection records the packet is measured against |
| `0x02` | raid | whether the raid roster is filled and `GetNumRaidMembers` answers |
| `0x08` | LFG | whether the conditional `u8 + u32` block is present in the header |

The two last-seen records are keyed on the **battleground** bit, not on raid — a battleground group
and a normal one are tracked apart so entering one does not make the other look stale. A packet is
dropped outright when the same group guid arrives with a counter no higher than the last one **and**
within 60 seconds. That dedupe is behaviour, not an optimisation — a port without it will rebuild
the roster on messages the reference discards.

Each member record is:

```
cstring name        // into a 48-byte buffer
u64     guid
u8      online
u8      subgroup
u8      flags
u8      roles
```

followed, after the last member, by `u64 leaderGuid` and then the loot method, looter guid, loot
threshold and the two difficulty bytes.

Reading the sequence out of the handler needed the copy loop separated from the parse, and the
readers identified first: frozen's `CDataStore` getters carry no reference tags, so
`FUN_0047b340`/`b3c0`/`b400`/`b440`/`b480` were pinned by how many bytes each advances the cursor
(1, 4, 8, 4, and a bounded string). That also confirmed the minimap ping's format after the fact.

## Status

Ported 2026-09-19. `CGPartyInfo` holds a `PARTY_MEMBER` per slot (guid, name, online, subgroup,
flags, roles) plus the leader, and `ReceiveGroupList` rebuilds it.

The raid roster comes from the same packet and the same member records — the subgroup field is what
separates the two views. `CGRaidInfo` keeps every member; `CGPartyInfo` keeps only those sharing the
player's subgroup.

`GetNumRaidMembers` is the roster **plus one**, because the packet does not list the player. The
reference computes `memberCount + 1`, and answers 0 both for a non-raid group and for a raid whose
member list is empty — the second case matters, or an empty roster would report 1.

Still missing: `raid1`-`raid40` as unit tokens. Membership is answerable, but the index order is
not: the player belongs somewhere in that numbering and the packet does not say where, so the
tokens stay unresolved rather than guessing a position.
