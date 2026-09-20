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
| `DAT_00bd1968` / `DAT_00bd196c` | the group guid |
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

`groupType & 0x01` distinguishes raid from party, and the handler keeps two separate
last-seen records so it can drop a duplicate: if the same group guid arrives with a counter no
higher than the last one **and** within 60 seconds, the packet is ignored outright. That dedupe is
behaviour, not an optimisation — a port without it will rebuild the roster on messages the
reference discards.

The member record layout is not transcribed here yet. The handler interleaves parsing with saving
the previous roster for comparison, so the read sequence has to be separated from the copy loop
before it can be trusted.

## What porting it needs

`CGPartyInfo` currently holds four guids and nothing else. The reference keeps a name, subgroup,
online flag, class and roles per member, which the party frames need. Growing that structure is the
first step, not the packet parse.
