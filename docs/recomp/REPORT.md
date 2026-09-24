# Recomp map: reference 3.3.5a (12340) vs frozen

Generated 2026-09-24 02:39 by `tools/recomp/recomp.py`. Do not edit; put facts in `tools/recomp/overrides.json`
or `// ref: FUN_xxxxxxxx` tags above frozen definitions and re-run.

## Totals

| | functions | code bytes |
|---|---:|---:|
| reference (non-thunk) | 27160 | 5.39M |
| mapped to a frozen function | 3180 (=) (11.7%) | 710.3k (12.9%) |
| &nbsp;&nbsp;ported | 2430 (=) | 525.6k |
| &nbsp;&nbsp;stub (unimplemented body) | 690 (=) | 137.7k |
| &nbsp;&nbsp;verified (override) | 14 (=) | 3.0k |
| **faithful** (linked, not stub, call order >= 80%) | **1248 (=) (4.6%)** | **164.4k (3.0%)** |
| unmapped | 23980 | 4.69M |
| world spine (reachable from OnFrameRender) | 5529, mapped 564 (=) (10.2%) | |
| &nbsp;&nbsp;render spine (world update + map + M2 scene) | 5797, mapped 574 (=) (9.9%) | |
| **render surface** (the modules that draw the world) | **4589, mapped 276 (=) (6.0%)** | |
| frozen functions (src/, from PDB + source) | 12306, stubs 1819 | |

Match evidence: annotated 953, callgraph 182, callorder 127, cvar 32, handler 29, order 150, override 260, sticky 21, string 319, table 1107. Module anchors: 1479 assert strings.

`order` and `sticky` are POSITIONAL: neither says anything about what the function does, only where it sits. `order` lays frozen's definitions onto the reference's addresses between two anchors whenever the two counts happen to be equal, so one unported function or one helper frozen invented shifts every pair in the run. It has been checked closely three times, each time because a fresh tag put a run next to something already read, and all three runs were wrong. Treat a link of either kind as a question. See tools/recomp/README.md under the order matcher.

Previous run: 2026-09-24 02:37 -- mapped 3180, ported 2430, stub 690, spine mapped 564.

## Lua API coverage (binding tables)

The reference registers 2964 Lua bindings across 100 tables (widget methods per class, and the global function blocks). frozen registers 2924 of them (=); 1493 of those are stubs (a WHOA_UNIMPLEMENTED body, or one of the WHOA_LUA_STUB bindings in MiscScriptStubs.cpp) (=). A missing name is a FrameXML call that raises "attempt to call a nil value"; a stub returns nothing, which is the arity bug class tools/arity.py hunts.

| ref table | entries | frozen array | missing | stubbed | first missing / stubbed names |
|---|---:|---|---:|---:|---|
| 00ac80b0 (FrameXML_Debug..) | 310 | `s_ScriptFunctions` | 0 | 168 |  / stubs: FrameXML_Debug, ReloadUI, RegisterForSave, RegisterForSavePerCharacter ... |
| 00ad0030 (CalendarGetMonthNames..) | 95 | `s_stubs` | 0 | 93 |  / stubs: CalendarGetMonthNames, CalendarGetWeekdayNames, CalendarGetMinDate, CalendarGetMaxDate ... |
| 00ac7a58 (SendChatMessage..) | 89 | `s_ScriptFunctions, s_stubs` | 0 | 73 |  / stubs: SendChatMessage, SendAddonMessage, SendSystemMessage, GetLanguageByIndex ... |
| 00acd668 (SetLFGDungeon..) | 67 | `s_ScriptFunctions, s_stubs` | 0 | 56 |  / stubs: SetLFGDungeon, ClearLFGDungeon, ClearAllLFGDungeons, GetLFGInfoLocal ... |
| 00ad1020 (GetNumQuestLogEntries..) | 67 | `s_ScriptFunctions, s_stubs` | 0 | 52 |  / stubs: GetQuestLogTitle, GetAbandonQuestName, GetAbandonQuestItems, AbandonQuest ... |
| 00ac3e00 (IsShiftKeyDown..) | 113 | `s_ScriptFunctions` | 0 | 50 |  / stubs: SetUsesToken, SetSavedAccountList, QuitGameAndRunLauncher, LaunchURL ... |
| 00acef78 (GetGossipText..) | 59 | `s_ScriptFunctions, s_stubs` | 0 | 48 |  / stubs: GetGossipText, GetGossipOptions, GetGossipAvailableQuests, GetGossipActiveQuests ... |
| 00accae8 (BNGetInfo..) | 57 | `s_ScriptFunctions` | 0 | 47 |  / stubs: BNGetInfo, BNGetFriendInfo, BNGetFriendInfoByID, BNGetNumFriendToons ... |
| 00ad1938 (JumpOrAscendStart..) | 52 | `s_ScriptFunctions, s_stubs` | 0 | 47 |  / stubs: JumpOrAscendStart, AscendStop, DescendStop, ToggleRun ... |
| 00acd178 (GetNumBattlefields..) | 51 | `s_ScriptFunctions` | 0 | 41 |  / stubs: GetNumBattlefields, GetBattlefieldInfo, GetBattlefieldInstanceInfo, JoinBattlefield ... |
| 00ad2ae0 (AddFontStrings..) | 69 | `CGTooltipMethods` | 0 | 39 |  / stubs: AddTexture, SetPetAction, SetShapeshift, SetPossession ... |
| 00ace370 (CommentatorSetMode..) | 35 | `s_stubs` | 0 | 35 |  / stubs: CommentatorSetMode, CommentatorToggleMode, CommentatorGetMode, CommentatorSetMapAndInstanceIndex ... |
| 00ad0e80 (CloseTradeSkill..) | 36 | `s_ScriptFunctions, s_stubs` | 0 | 34 |  / stubs: CloseTradeSkill, GetTradeSkillInfo, SelectTradeSkill, GetTradeSkillSelectionIndex ... |
| 00ace610 (CloseMail..) | 38 | `s_ScriptFunctions, s_stubs` | 0 | 33 |  / stubs: CloseMail, ClearSendMail, ClickSendMailItemButton, SetSendMailMoney ... |
| 00ad21d8 (UnitExists..) | 169 | `s_UnitFunctions` | 0 | 33 |  / stubs: UnitAttackBothHands, UnitRangedAttack, UnitDefense, SetPortraitTexture ... |
| 00acfc70 (GetCategoryList..) | 37 | `s_ScriptFunctions, s_stubs` | 0 | 30 |  / stubs: GetCategoryList, GetStatisticsCategoryList, GetCategoryInfo, GetCategoryNumAchievements ... |
| 00accf18 (GetMapContinents..) | 40 | `s_ScriptFunctions, s_stubs` | 0 | 27 |  / stubs: GetMapContinents, GetMapZones, SetMapZoom, ZoomOut ... |
| 00ad0840 (GetNumGuildMembers..) | 43 | `s_ScriptFunctions, s_stubs` | 0 | 27 |  / stubs: GetGuildRosterInfo, GetGuildRosterLastOnline, GuildRosterSetPublicNote, GuildRosterSetOfficerNote ... |
| 00acf3e8 (OpenTrainer..) | 28 | `s_ScriptFunctions, s_stubs` | 0 | 25 |  / stubs: OpenTrainer, CloseTrainer, GetTrainerServiceInfo, SelectTrainerService ... |
| 00acf8d0 (QueryGuildBankTab..) | 29 | `s_ScriptFunctions, s_stubs` | 0 | 25 |  / stubs: QueryGuildBankTab, SetCurrentGuildBankTab, GetCurrentGuildBankTab, GetGuildBankItemInfo ... |
| 00acf638 (CloseAuctionHouse..) | 30 | `s_ScriptFunctions, s_stubs` | 0 | 24 |  / stubs: CloseAuctionHouse, GetAuctionHouseDepositRate, CalculateAuctionDeposit, ClickAuctionSellItemButton ... |
| 00ad0c30 (PetHasActionBar..) | 31 | `s_ScriptFunctions, s_stubs` | 0 | 24 |  / stubs: GetPetActionsUsable, GetPetActionSlotUsable, PickupPetAction, TogglePetAutocast ... |
| 00ad93d8 (GetNumFriends..) | 31 | `s_ScriptFunctions, s_stubs` | 0 | 23 |  / stubs: GetFriendInfo, SetSelectedFriend, GetSelectedFriend, AddOrRemoveFriend ... |
| 00acd488 (KBSetup_BeginLoading..) | 22 | `-` | 22 | 0 | KBSetup_BeginLoading, KBSetup_IsLoaded, KBSetup_GetLanguageCount, KBSetup_GetLanguageData, KBSetup_GetCategoryCount, KBSetup_GetCategoryData ... |
| 00ad2060 (CameraZoomIn..) | 22 | `s_stubs` | 0 | 22 |  / stubs: CameraZoomIn, CameraZoomOut, MoveViewInStart, MoveViewInStop ... |
| 00ace1f0 (SecureCmdOptionParse..) | 22 | `s_ScriptFunctions, s_stubs` | 0 | 19 |  / stubs: SecureCmdOptionParse, RunMacro, RunMacroText, StopMacro ... |
| 00accce0 (GetNumSpellTabs..) | 45 | `s_ScriptFunctions` | 0 | 18 |  / stubs: GetSpellCount, ToggleSpellAutocast, EnableSpellAutocast, DisableSpellAutocast ... |
| 00aced00 (CloseMerchant..) | 21 | `s_ScriptFunctions, s_stubs` | 0 | 18 |  / stubs: CloseMerchant, GetMerchantNumItems, GetMerchantItemInfo, GetMerchantItemCostInfo ... |
| 00ad1270 (GetInventorySlotInfo..) | 33 | `s_ScriptFunctions` | 0 | 18 |  / stubs: GetInventoryItemsForSlot, GetInventoryItemCooldown, GetInventoryItemGems, PickupInventoryItem ... |
| 00acfb78 (SaveEquipmentSet..) | 17 | `s_ScriptFunctions, s_stubs` | 0 | 15 |  / stubs: SaveEquipmentSet, DeleteEquipmentSet, RenameEquipmentSet, EquipmentManagerIgnoreSlotForSave ... |
| 00acde38 (GetNumBindings..) | 26 | `s_ScriptFunctions` | 0 | 14 |  / stubs: SetBindingSpell, SetBindingItem, SetBindingMacro, SetBindingClick ... |
| 00acee28 (SetLootPortrait..) | 17 | `s_ScriptFunctions, s_stubs` | 0 | 14 |  / stubs: SetLootPortrait, GetLootSlotInfo, GetLootSlotLink, LootSlotIsItem ... |
| 00acfaf8 (GetGMTicket..) | 15 | `s_ScriptFunctions` | 0 | 14 |  / stubs: GetGMTicket, NewGMTicket, UpdateGMTicket, DeleteGMTicket ... |
| 00b2d928 (PlaySound..) | 23 | `SI2::s_ScriptFunctions` | 0 | 14 |  / stubs: PlayMusic, PlaySoundFile, StopMusic, Sound_GameSystem_RestartSoundSystem ... |
| 00ace790 (GetNumRaidMembers..) | 20 | `s_ScriptFunctions` | 0 | 13 |  / stubs: GetRaidRosterInfo, SetRaidRosterSelection, GetRaidRosterSelection, IsRealRaidLeader ... |
| 00acf258 (SetTaxiMap..) | 14 | `s_ScriptFunctions, s_stubs` | 0 | 13 |  / stubs: SetTaxiMap, NumTaxiNodes, TaxiNodeName, TaxiNodePosition ... |
| 00ad0ae8 (GetNumFactions..) | 15 | `s_ScriptFunctions, s_stubs` | 0 | 13 |  / stubs: GetFactionInfo, GetFactionInfoByID, GetWatchedFactionInfo, SetWatchedFactionIndex ... |
| 00a443a8 (setglobal..) | 30 | `FrameScriptInternal::extra_funcs` | 0 | 12 |  / stubs: issecurevariable, forceinsecure, hooksecurefunc, debugload ... |
| 00acedb0 (CloseTrade..) | 14 | `s_ScriptFunctions` | 0 | 12 |  / stubs: CloseTrade, ClickTradeButton, ClickTargetTradeButton, GetTradeTargetItemInfo ... |
| 00ad0d50 (ContainerIDToInventoryID..) | 22 | `s_ScriptFunctions` | 0 | 12 |  / stubs: GetContainerItemCooldown, PickupContainerItem, SplitContainerItem, UseContainerItem ... |
| 00acd418 (AccountMsg_LoadHeaders..) | 11 | `-` | 11 | 0 | AccountMsg_LoadHeaders, AccountMsg_GetNumTotalMsgs, AccountMsg_GetNumUnreadMsgs, AccountMsg_GetNumUnreadUrgentMsgs, AccountMsg_GetIndexHighestPriorityUnreadMsg, AccountMsg_GetIndexNextUnreadMsg ... |
| 00acf768 (ClosePetStables..) | 14 | `s_ScriptFunctions, s_stubs` | 0 | 11 |  / stubs: ClosePetStables, StablePet, UnstablePet, BuyStableSlot ... |
| 00ad0568 (CloseSocketInfo..) | 12 | `s_ScriptFunctions, s_stubs` | 0 | 11 |  / stubs: CloseSocketInfo, GetSocketItemInfo, GetExistingSocketInfo, GetExistingSocketLink ... |
| 00adb8e0 (CombatLogResetFilter..) | 11 | `s_ScriptFunctions, s_stubs` | 0 | 10 |  / stubs: CombatLogResetFilter, CombatLogAddFilter, CombatLogSetRetentionTime, CombatLogGetRetentionTime ... |
| 00af29c0 (VoiceEnumerateOutputDevices..) | 15 | `s_ScriptFunctions, s_stubs` | 0 | 10 |  / stubs: VoiceEnumerateOutputDevices, VoiceEnumerateCaptureDevices, VoiceSelectOutputDevice, VoiceSelectCaptureDevice ... |
| 00aceec0 (ItemTextGetItem..) | 9 | `s_stubs` | 0 | 9 |  / stubs: ItemTextGetItem, ItemTextGetCreator, ItemTextGetMaterial, ItemTextGetPage ... |
| 00acf838 (GetArenaTeam..) | 13 | `s_ScriptFunctions, s_stubs` | 0 | 9 |  / stubs: GetArenaTeamRosterInfo, GetArenaTeamGdfInfo, SetArenaTeamRosterSelection, GetArenaTeamRosterSelection ... |
| 00ad05f0 (GetNumTalentTabs..) | 17 | `s_ScriptFunctions, s_stubs` | 0 | 9 |  / stubs: GetTalentInfo, GetTalentLink, GetTalentPrereqs, LearnTalent ... |
| 00af51c8 (SpellIsTargeting..) | 11 | `s_ScriptFunctions, s_stubs` | 0 | 8 |  / stubs: SpellIsTargeting, SpellCanTargetItem, SpellTargetItem, SpellCanTargetUnit ... |
| 00acc6d0 (GetNumPartyMembers..) | 22 | `s_ScriptFunctions` | 0 | 7 |  / stubs: LeaveParty, SetPartyAssignment, ClearPartyAssignment, SilenceMember ... |
| 00acf9e0 (GetActionInfo..) | 28 | `s_ScriptFunctions` | 0 | 7 |  / stubs: GetActionAutocast, PickupAction, PlaceAction, IsConsumableAction ... |
| 00ad09b8 (GetNumSkillLines..) | 13 | `s_ScriptFunctions, s_stubs` | 0 | 7 |  / stubs: AbandonSkill, CollapseSkillHeader, ExpandSkillHeader, AddSkillUp ... |
| 00ac1550 (GetTitleRegion..) | 85 | `SimpleFrameMethods` | 0 | 6 |  / stubs: CreateTitleRegion, HookScript, AllowAttributeChanges, CanChangeAttribute ... |
| 00acf588 (InitializeTabardColors..) | 10 | `CGTabardModelFrameMethods` | 0 | 6 |  / stubs: InitializeTabardColors, Save, CycleVariation, GetUpperEmblemTexture ... |
| 00acf7f0 (ClosePetitionVendor..) | 8 | `s_ScriptFunctions, s_stubs` | 0 | 6 |  / stubs: ClosePetitionVendor, GetPetitionItemInfo, BuyPetition, ClickPetitionButton ... |
| 00acfc34 (GetCurrencyListSize..) | 6 | `s_stubs` | 0 | 6 |  / stubs: GetCurrencyListSize, GetCurrencyListInfo, ExpandCurrencyList, SetCurrencyUnused ... |
| 00ad0a60 (ClosePetition..) | 8 | `s_ScriptFunctions, s_stubs` | 0 | 6 |  / stubs: ClosePetition, GetPetitionInfo, GetPetitionNameInfo, SignPetition ... |
| 00ac4228 (SetCharCustomizeFrame..) | 32 | `s_ScriptFunctions` | 0 | 5 |  / stubs: CustomizeExistingCharacter, PaidChange_GetPreviousRaceIndex, PaidChange_GetCurrentRaceIndex, PaidChange_GetCurrentClassIndex ... |
| 00acc790 (GetBarberShopStyleInfo..) | 9 | `s_ScriptFunctions, s_stubs` | 0 | 5 |  / stubs: GetBarberShopStyleInfo, SetNextBarberShopStyle, GetBarberShopTotalCost, ApplyBarberShopStyle ... |
| 00acc868 (CanResetTutorials..) | 8 | `s_ScriptFunctions, s_stubs` | 0 | 5 |  / stubs: FlagTutorial, ClearTutorials, ResetTutorials, GetNextCompleatedTutorial ... |
| 00acf5fc (CloseGuildRegistrar..) | 5 | `s_stubs` | 0 | 5 |  / stubs: CloseGuildRegistrar, GetGuildCharterCost, BuyGuildCharter, TurnInGuildCharter ... |
| 00acff54 (GetNumGlyphSockets..) | 6 | `s_ScriptFunctions, s_stubs` | 0 | 5 |  / stubs: GetGlyphSocketInfo, GlyphMatchesSocket, PlaceGlyphInSocket, RemoveGlyphFromSocket ... |
| 00af5848 (GetText..) | 7 | `s_ScriptFunctions` | 0 | 5 |  / stubs: GetNumFrames, EnumerateFrames, CreateFont, GetFramesRegisteredForEvent ... |
| 00b2cee0 (SetFontObject..) | 48 | `SimpleScrollingMessageFrameMethods` | 5 | 0 | GetMessageInfo, RemoveMessagesByAccessID, SetScrollOffset, UpdateColorByID, GetCurrentLine |
| 00ac4190 (RequestRealmList..) | 14 | `s_ScriptFunctions` | 0 | 4 |  / stubs: SortRealms, IsInvalidTournamentRealmCategory, IsTournamentRealmCategory, IsInvalidLocale |
| 00aceb7c (BankButtonIDToInvSlotID..) | 5 | `s_ScriptFunctions, s_stubs` | 0 | 4 |  / stubs: BankButtonIDToInvSlotID, GetBankSlotCost, PurchaseSlot, CloseBankFrame |
| 00acf180 (SetFillTexture..) | 14 | `CGQuestPOIFrameMethods` | 0 | 4 |  / stubs: SetFillTexture, SetBorderTexture, DrawQuestBlob, UpdateMouseOverTooltip |
| 00b2cb10 (SetModel..) | 24 | `SimpleModelMethods` | 0 | 4 |  / stubs: SetLight, GetModelScale, ReplaceIconTexture, SetGlow |
| 00ac465c (ResetLights..) | 4 | `SimpleModelFFXMethods` | 0 | 3 |  / stubs: AddLight, AddCharacterLight, AddPetLight |
| 00acf514 (SetUnit..) | 4 | `CGCharacterModelBaseMethods` | 0 | 3 |  / stubs: SetUnit, SetCreature, RefreshUnit |
| 00ac1480 (IsProtected..) | 25 | `ScriptRegionMethods` | 0 | 2 |  / stubs: IsProtected, CanChangeProtectedState |
| 00ac4378 (SetCharSelectModelFrame..) | 13 | `s_ScriptFunctions` | 0 | 2 |  / stubs: RenameCharacter, DeclineCharacter |
| 00b2cd50 (GetColorWheelTexture..) | 12 | `SimpleColorSelectMethods` | 2 | 0 | SetColorWheelThumbTexture, SetColorValueThumbTexture |
| 00b2cdb8 (GetOrientation..) | 12 | `SimpleStatusBarMethods` | 0 | 2 |  / stubs: SetOrientation, SetStatusBarTexture |
| 00b2ce20 (GetThumbTexture..) | 13 | `SimpleSliderMethods` | 0 | 2 |  / stubs: SetThumbTexture, SetOrientation |
| 00b2d3e4 (SetChecked..) | 6 | `SimpleCheckboxMethods` | 0 | 2 |  / stubs: SetCheckedTexture, SetDisabledCheckedTexture |
| 00ac1028 (IsObjectType..) | 29 | `SimpleTextureMethods` | 0 | 1 |  / stubs: SetGradient |
| 00ac18e0 (Play..) | 31 | `SimpleAnimMethods` | 0 | 1 |  / stubs: HookScript |
| 00ac1ab8 (Play..) | 26 | `SimpleAnimGroupMethods` | 0 | 1 |  / stubs: HookScript |
| 00ad13a4 (SetCooldown..) | 5 | `CGCooldownMethods` | 0 | 1 |  / stubs: SetCooldown |
| 00ad2184 (GetTime..) | 7 | `s_SystemFunctions` | 0 | 1 |  / stubs: ConsoleExec |
| 00b2ce90 (SetScrollChild..) | 9 | `SimpleScrollFrameMethods` | 0 | 1 |  / stubs: SetHorizontalScroll |
| 00b2d210 (SetFontObject..) | 58 | `SimpleEditBoxMethods` | 0 | 1 |  / stubs: ClearFocus |
| 00b2d418 (Enable..) | 34 | `SimpleButtonMethods` | 0 | 1 |  / stubs: SetFontString |

## Packet handler coverage (SetMessageHandler)

The reference registers handlers for 579 opcodes; frozen registers 41 of them. An opcode with no frozen handler is a server message the client silently drops.

Reference-only, by opcode (handler address): SMSG_CHECK_FOR_BOTS 006b9670, SMSG_FORCEACTIONSHOW 006e2e90, SMSG_PETGODMODE 006e2e90, SMSG_REFER_A_FRIEND_EXPIRED 00526530, SMSG_GOD_MODE 006e2e90, SMSG_DESTRUCTIBLE_BUILDING_DAMAGE 00753730, SMSG_QUERY_PET_NAME_RESPONSE 006352c0, SMSG_QUERY_GUILD_INFO_RESPONSE 006359e0, SMSG_ITEM_QUERY_MULTIPLE_RESPONSE 006351f0, SMSG_QUERY_PAGE_TEXT_RESPONSE 006352a0, SMSG_QUERY_QUEST_INFO_RESPONSE 00635230, SMSG_QUERY_GAME_OBJECT_RESPONSE 006351b0, SMSG_WHO 006b8720, SMSG_WHO_IS 006b3280, SMSG_CONTACT_LIST 006b8700, SMSG_FRIEND_STATUS 006b86b0, SMSG_GROUP_INVITE 006df1a0, SMSG_GROUP_CANCEL 006cbd10, SMSG_GROUP_DECLINE 006cbd40, SMSG_GROUP_SET_LEADER 006cbd70, SMSG_PARTY_MEMBER_STATS 006cf9b0, SMSG_PARTY_COMMAND_RESULT 006cbec0, SMSG_GUILD_INVITE 006cc3b0, SMSG_GUILD_DECLINE 006cc410, SMSG_GUILD_INFO 006cc440, SMSG_GUILD_ROSTER 005cc5d0, SMSG_GUILD_EVENT 006d92d0, SMSG_GUILD_COMMAND_RESULT 006cc590, SMSG_CHAT 0050eba0, SMSG_CHANNEL_NOTIFY 0050e120, SMSG_CHANNEL_LIST 00505dc0, SMSG_READ_ITEM_RESULT_OK 006dbdf0, SMSG_READ_ITEM_RESULT_FAILED 006dbdf0, SMSG_ITEM_COOLDOWN 00807060, SMSG_GAME_OBJECT_CUSTOM_ANIM 0070be90, MSG_MOVE_START_FORWARD 00741b60, MSG_MOVE_START_BACKWARD 00741b60, MSG_MOVE_STOP 00741b60, MSG_MOVE_START_STRAFE_LEFT 00741b60, MSG_MOVE_START_STRAFE_RIGHT 00741b60, MSG_MOVE_STOP_STRAFE 00741b60, MSG_MOVE_JUMP 00741b60, MSG_MOVE_START_TURN_LEFT 00741b60, MSG_MOVE_START_TURN_RIGHT 00741b60, MSG_MOVE_STOP_TURN 00741b60, MSG_MOVE_START_PITCH_UP 00741b60, MSG_MOVE_START_PITCH_DOWN 00741b60, MSG_MOVE_STOP_PITCH 00741b60, MSG_MOVE_SET_RUN_MODE 00741b60, MSG_MOVE_SET_WALK_MODE 00741b60, MSG_MOVE_TELEPORT 00741b60, MSG_MOVE_TELEPORT_ACK 00732450, MSG_MOVE_FALL_LAND 00741b60, MSG_MOVE_START_SWIM 00741b60, MSG_MOVE_STOP_SWIM 00741b60, MSG_MOVE_SET_RUN_SPEED 00741b00, MSG_MOVE_SET_RUN_BACK_SPEED 00741b00, MSG_MOVE_SET_WALK_SPEED 00741b00, MSG_MOVE_SET_SWIM_SPEED 00741b00, MSG_MOVE_SET_SWIM_BACK_SPEED 00741b00, MSG_MOVE_SET_TURN_RATE 00741b00, MSG_MOVE_TOGGLE_COLLISION_CHEAT 00741b60, MSG_MOVE_SET_FACING 00741b60, MSG_MOVE_SET_PITCH 00741b60, SMSG_ON_MONSTER_MOVE 0073f590, SMSG_MOVE_SET_WATER_WALK 00732450, SMSG_MOVE_SET_LAND_WALK 00732450, SMSG_FORCE_RUN_SPEED_CHANGE 00732450, SMSG_FORCE_RUN_BACK_SPEED_CHANGE 00732450, SMSG_FORCE_SWIM_SPEED_CHANGE 00732450, SMSG_FORCE_MOVE_ROOT 00732450, SMSG_FORCE_MOVE_UNROOT 00732450, 0xec 00741b60, 0xed 00741b60, 0xee 00741b60, SMSG_MOVE_KNOCK_BACK 00732450, 0xf1 00741b60, SMSG_MOVE_SET_FEATHER_FALL 00732450, SMSG_MOVE_SET_NORMAL_FALL 00732450, SMSG_MOVE_SET_HOVERING 00732450, SMSG_MOVE_UNSET_HOVERING 00732450, 0xf7 00741b60, SMSG_TRIGGER_CINEMATIC 006e2e90, SMSG_TUTORIAL_FLAGS 00530920, SMSG_TEXT_EMOTE 00504070, SMSG_OPEN_CONTAINER 006e2e90, SMSG_INSPECT_RESULTS_UPDATE 006ce070, SMSG_TRADE_STATUS 007044a0, SMSG_TRADE_STATUS_EXTENDED 00704680, SMSG_INITIALIZE_FACTIONS 005d2e30, SMSG_SET_FACTION_VISIBLE 005d2050, SMSG_SET_FACTION_STANDING 005d20a0, SMSG_SET_PROFICIENCY 006cdeb0, SMSG_SUPERCEDED_SPELLS 006e7e00, SMSG_CAST_FAILED 00809af0, SMSG_SPELL_COOLDOWN 00806dd0, SMSG_COOLDOWN_EVENT 00804010, SMSG_EQUIPMENT_SET_ID 005af6c0, SMSG_PET_CAST_FAILED 00806c30, SMSG_AI_REACTION 00716b10, SMSG_ATTACK_START 00756800, SMSG_ATTACK_STOP 00756800, SMSG_ATTACKSWING_NOTINRANGE 00756800, SMSG_ATTACKSWING_BADFACING 00756800, SMSG_INSTANCE_LOCK_WARNING_QUERY 006e2e90, SMSG_ATTACKSWING_DEADTARGET 00756800, SMSG_ATTACKSWING_CANT_ATTACK 00756800, SMSG_ATTACKER_STATE_UPDATE 00756800, SMSG_RESUME_CAST_BAR 008020c0, SMSG_SPELL_BREAK_LOG 006ce3b0, SMSG_SPELL_HEAL_LOG 006d3dd0, SMSG_SPELL_ENERGIZE_LOG 006d3ef0, SMSG_BREAK_TARGET 00526530, SMSG_BIND_POINT_UPDATE 006e2e90, SMSG_BINDZONEREPLY 006e2e90, SMSG_PLAYER_BOUND 006e2e90, SMSG_CONTROL_UPDATE 0072d0b0, SMSG_RESURRECT_REQUEST 006dbd00, SMSG_LOOT_RESPONSE 006d84f0, SMSG_LOOT_RELEASE 006d84f0, SMSG_LOOT_REMOVED 006d84f0, SMSG_LOOT_MONEY_NOTIFY 006d84f0, SMSG_LOOT_ITEM_NOTIFY 006d84f0, SMSG_LOOT_CLEAR_MONEY 006d84f0, SMSG_ITEM_PUSH_RESULT 006e2e90, SMSG_DUEL_REQUESTED 005cfcd0, SMSG_DUEL_OUT_OF_BOUNDS 005cf910, SMSG_DUEL_IN_BOUNDS 005cf930, SMSG_DUEL_COMPLETE 005cfa90, SMSG_DUEL_WINNER 005cfb20, SMSG_MOUNT_RESULT 006e2e90, SMSG_DISMOUNT_RESULT 006e2e90, SMSG_REMOVED_FROM_PVP_QUEUE 00526530, SMSG_MOUNT_SPECIAL_ANIM 0071cb30, SMSG_PET_TAME_FAILURE 00802090, SMSG_PET_NAME_INVALID 006e2e90, SMSG_PET_SPELLS_MESSAGE 005d6b90, SMSG_PET_MODE 005d3630, SMSG_GOSSIP_MESSAGE 0058b1b0, SMSG_GOSSIP_COMPLETE 0058a840, SMSG_QUERY_NPC_TEXT_RESPONSE 00635210, SMSG_QUEST_GIVER_QUEST_LIST_MESSAGE 006d7f10, SMSG_QUEST_GIVER_QUEST_DETAILS 006d7f10, SMSG_QUEST_GIVER_REQUEST_ITEMS 006d7f10, SMSG_QUEST_GIVER_OFFER_REWARD_MESSAGE 006d7f10, SMSG_QUEST_GIVER_INVALID_QUEST 006d7f10, SMSG_QUEST_GIVER_QUEST_COMPLETE 006d7f10, SMSG_QUEST_GIVER_QUEST_FAILED 006d7f10, SMSG_QUEST_LOG_FULL 006d7f10, SMSG_QUEST_UPDATE_FAILED 006d8030, SMSG_QUEST_UPDATE_FAILED_TIMER 006d8030, SMSG_QUEST_UPDATE_COMPLETE 006d8030, SMSG_QUEST_UPDATE_ADD_KILL 006d8030, SMSG_QUEST_CONFIRM_ACCEPT 006cbc50, SMSG_VENDOR_INVENTORY 006defa0, SMSG_SELL_ITEM 006defa0, SMSG_BUY_SUCCEEDED 006defa0, SMSG_BUY_FAILED 006defa0, SMSG_SHOW_TAXI_NODES 006e2e90, SMSG_TAXI_NODE_STATUS 006e2e90, SMSG_ACTIVATE_TAXI_REPLY 006e2e90, SMSG_NEW_TAXI_PATH 006e2e90, SMSG_TRAINER_LIST 006d8410, SMSG_TRAINER_BUY_FAILED 006d8410, SMSG_PLAYERBINDERROR 006e2e90, SMSG_SHOW_BANK 006e2e90, SMSG_BUY_BANK_SLOT_RESULT 006e2e90, SMSG_PETITION_SHOW_LIST 006deef0, SMSG_PETITION_SHOW_SIGNATURES 006deef0, SMSG_PETITION_SIGN_RESULTS 006deef0, 0x1c2 006deef0, SMSG_TURN_IN_PETITION_RESULT 006deef0, SMSG_PETITION_QUERY_RESPONSE 00635390, SMSG_FISH_NOT_HOOKED 006e2e90, SMSG_FISH_ESCAPED 006e2e90, SMSG_QUERY_TIME_RESPONSE 005e08a0, SMSG_LOG_XP_GAIN 0050c720, SMSG_LEVEL_UP_INFO 006e2e90, SMSG_ENCHANTMENT_LOG 00751050, SMSG_START_MIRROR_TIMER 00519a50, SMSG_PAUSE_MIRROR_TIMER 00519a50, SMSG_STOP_MIRROR_TIMER 00519a50, SMSG_CLEAR_COOLDOWN 00804010, SMSG_PAGE_TEXT 0070be30, SMSG_COOLDOWN_CHEAT 00804110, SMSG_SPELL_DELAYED 00801b80, SMSG_QUEST_POI_QUERY_RESPONSE 006cbcb0, SMSG_INVALID_PROMOTION_CODE 00526530, SMSG_ITEM_TIME_UPDATE 006e6330, SMSG_ITEM_ENCHANT_TIME_UPDATE 006e6330, 0x1f1 006d9230, 0x1f2 006cc560, SMSG_PLAY_SPELL_VISUAL 00800610, SMSG_PARTY_KILL_LOG 00750c90, SMSG_PLAY_SPELL_IMPACT 008006c0, SMSG_EXPLORATION_EXPERIENCE 006e2e90, CMSG_RANDOM_ROLL 006e2e90, SMSG_ENVIRONMENTAL_DAMAGE_LOG 00756800, SMSG_RWHOIS 006b32c0, SMSG_LFG_PLAYER_REWARD 0055bdc0, SMSG_LFG_TELEPORT_DENIED 0055bdc0, SMSG_GM_TICKET_CREATE 005ad240, SMSG_GM_TICKET_UPDATE_TEXT 005ad240, SMSG_ACCOUNT_DATA_TIMES 006b94c0, SMSG_UPDATE_ACCOUNT_DATA 006b9730, SMSG_CLEAR_FAR_SIGHT_IMMEDIATE 006e2e90, SMSG_CHANGE_PLAYER_DIFFICULTY_RESULT 0052c460, SMSG_GM_TICKET_GET_TICKET 005ad240, SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT 005eddd0, SMSG_GAMEOBJECT_DESPAWN_ANIM 0070bef0, 0x216 00526530, SMSG_GM_TICKET_DELETE_TICKET 005ad240, SMSG_CHAT_WRONG_FACTION 006e2e90, SMSG_GM_TICKET_GET_SYSTEM_STATUS 006e2e90, SMSG_QUEST_FORCE_REMOVED 006d8370, SMSG_SPIRIT_HEALER_CONFIRM 006e2e90, SMSG_GOSSIP_POI 0058a870, CMSG_GM_REQUEST_PLAYER_INFO 00526530, SMSG_GM_PLAYER_INFO 00526530, SMSG_MAIL_COMMAND_RESULT 005717b0, SMSG_MAIL_LIST_RESULT 00571c50, SMSG_BATTLEFIELD_LIST 0054e390, SMSG_QUERY_ITEM_TEXT_RESPONSE 00635a40, SMSG_SPELL_MISS_LOG 006d3200, SMSG_SPELL_EXECUTE_LOG 006d3730, SMSG_SPELL_PERIODIC_AURA_LOG 00753710, SMSG_SPELL_DAMAGE_SHIELD 006d3750, SMSG_SPELL_NON_MELEE_DAMAGE_LOG 006d3c10, SMSG_ZONE_UNDER_ATTACK 0050c3c0, 0x255 0059ff40, SMSG_AUCTION_COMMAND_RESULT 0059ffb0, SMSG_AUCTION_LIST_RESULT 0059e160, SMSG_AUCTION_LIST_OWNER_ITEMS_RESULT 0059e480, SMSG_AUCTION_BIDDER_NOTIFICATION 005a0480, SMSG_AUCTION_OWNER_NOTIFICATION 005a0790, SMSG_PROC_RESIST 00750d40, SMSG_COMBAT_EVENT_FAILED 00756800, SMSG_DISPEL_FAILED 00750ea0, SMSG_SPELL_OR_DAMAGE_IMMUNE 006ce3d0, SMSG_AUCTION_LIST_BIDDER_ITEMS_RESULT 0059ecd0, SMSG_SET_FLAT_SPELL_MODIFIER 007fdc60, SMSG_SET_PCT_SPELL_MODIFIER 007fdc60, SMSG_CORPSE_RECLAIM_DELAY 00526530, 0x26f 005a11a0, SMSG_STABLE_RESULT 005a17f0, 0x276 006e2e90, SMSG_PLAY_MUSIC 00526530, SMSG_PLAY_OBJECT_SOUND 00526530, SMSG_SPELL_DISPELL_LOG 006ce3b0, SMSG_DAMAGE_CALC_LOG 006d3f10, 0x284 0056daf0, SMSG_RECEIVED_MAIL 00571a10, SMSG_RAID_GROUP_ONLY 006e2e90, SMSG_PVP_CREDIT 00526530, SMSG_AUCTION_REMOVED_NOTIFICATION 005a0ac0, SMSG_CHAT_SERVER_MESSAGE 0050c980, SMSG_LFG_OFFER_CONTINUE 0055bdc0, SMSG_TEST_DROP_RATE_RESULT 006d84f0, SMSG_SHOW_MAILBOX 0056dbc0, SMSG_RESET_RANGED_COMBAT_TIMER 008071c0, SMSG_CHAT_NOT_IN_PARTY 006e2e90, SMSG_CANCEL_AUTO_REPEAT 0072d130, SMSG_STAND_STATE_UPDATE 0073f540, SMSG_LOOT_ALL_PASSED 006d84f0, SMSG_LOOT_ROLL_WON 006d84f0, SMSG_LOOT_START_ROLL 006d84f0, SMSG_LOOT_ROLL 006d84f0, SMSG_LOOT_MASTER_LIST 006d84f0, SMSG_SET_FORCED_REACTIONS 005d15d0, SMSG_GAME_OBJECT_RESET_STATE 007fd900, SMSG_CHAT_PLAYER_NOTFOUND 006e2e90, 0x2aa 006e2e90, SMSG_SUMMON_REQUEST 006d8680, SMSG_MONSTER_MOVE_TRANSPORT 0073f590, 0x2b0 00741b60, 0x2b1 00741b60, SMSG_DUEL_COUNTDOWN 005cfa50, SMSG_AREA_TRIGGER_MESSAGE 00526530, SMSG_LFG_ROLE_CHOSEN 0055bdc0, SMSG_PLAYER_SKINNED 006cdf00, 0x2c1 006deef0, SMSG_INIT_WORLD_STATES 00526530, SMSG_UPDATE_WORLD_STATE 00526530, SMSG_ITEM_NAME_QUERY_RESPONSE 006351d0, SMSG_PET_ACTION_FEEDBACK 005d4da0, SMSG_INSTANCE_SAVE_CREATED 0052bb10, SMSG_RAID_INSTANCE_INFO 00501030, SMSG_PLAY_SOUND 00526530, SMSG_BATTLEFIELD_STATUS 0054ae40, 0x2d6 005e7a50, SMSG_FORCE_WALK_SPEED_CHANGE 00732450, SMSG_FORCE_SWIM_BACK_SPEED_CHANGE 00732450, SMSG_FORCE_TURN_RATE_CHANGE 00732450, 0x2e0 0054d280, SMSG_AREA_SPIRIT_HEALER_TIME 00526530, SMSG_WARDEN_DATA 007da850, SMSG_BATTLEFIELD_STATUS_QUEUED 0054b1c0, 0x2e9 0054b3f0, SMSG_BINDER_CONFIRM 006e2e90, SMSG_BATTLEGROUND_PLAYER_JOINED 0054b510, SMSG_BATTLEGROUND_PLAYER_LEFT 0054b510, SMSG_PARTY_MEMBER_STATS_FULL 006cf9b0, SMSG_PLAY_TIME_WARNING 006cd270, SMSG_MINIGAME_SETUP 005c5690, SMSG_MINIGAME_STATE 005c54d0, SMSG_RAID_INSTANCE_MESSAGE 0050ca80, SMSG_COMPRESSED_MOVES 00716ad0, SMSG_CHAT_RESTRICTED 006e2e90, SMSG_MOVE_SPLINE_SET_RUN_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_RUN_BACK_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_SWIM_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_WALK_BACK_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_SWIM_BACK_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_TURN_RATE 00741bc0, SMSG_MOVE_SPLINE_UNROOT 00741c30, SMSG_MOVE_SPLINE_SET_FEATHER_FALL 00741c30, SMSG_MOVE_SPLINE_SET_NORMAL_FALL 00741c30, SMSG_MOVE_SPLINE_SET_HOVER 00741c30, SMSG_MOVE_SPLINE_UNSET_HOVER 00741c30, SMSG_MOVE_SPLINE_SET_WATER_WALK 00741c30, SMSG_MOVE_SPLINE_SET_LAND_WALK 00741c30, SMSG_MOVE_SPLINE_START_SWIM 00741c30, SMSG_MOVE_SPLINE_STOP_SWIM 00741c30, SMSG_MOVE_SPLINE_SET_RUN_MODE 00741c30, SMSG_MOVE_SPLINE_SET_WALK_MODE 00741c30, SMSG_SET_FACTION_AT_WAR 005d0850, 0x319 0071cab0, SMSG_MOVE_SPLINE_ROOT 00741c30, SMSG_INVALIDATE_PLAYER 00635400, SMSG_INSTANCE_RESET 0050ccd0, SMSG_INSTANCE_RESET_FAILED 0050cda0, SMSG_UPDATE_LAST_INSTANCE 004fe100, 0x322 00574d50, SMSG_PET_ACTION_SOUND 00716b90, SMSG_PET_DISMISS_SOUND 00716c00, SMSG_GM_TICKET_STATUS_UPDATE 005ad240, SMSG_UPDATE_INSTANCE_OWNERSHIP 004fb990, SMSG_SPELL_INSTAKILL_LOG 006ce260, SMSG_SPELL_UPDATE_CHAIN_TARGETS 00800470, SMSG_EXPECTED_SPAM_RECORDS 00501c70, SMSG_SPELL_STEAL_LOG 006ce3b0, SMSG_DEFENSE_MESSAGE 0050c850, SMSG_INSTANCE_DIFFICULTY 00526530, SMSG_MOTD 00551660, SMSG_MOVE_ENABLE_TRANSITION_BETWEEN_SWIM_AND_FLY 00732450, SMSG_MOVE_DISABLE_TRANSITION_BETWEEN_SWIM_AND_FLY 00732450, 0x341 00741b60, 0x342 00741b60, SMSG_MOVE_SET_CAN_FLY 00732450, SMSG_MOVE_UNSET_CAN_FLY 00732450, SMSG_ARENA_TEAM_COMMAND_RESULT 006ccb20, 0x34a 00741b60, SMSG_ARENA_TEAM_QUERY_RESPONSE 00635480, SMSG_ARENA_TEAM_ROSTER 005a3e10, SMSG_ARENA_TEAM_INVITE 006cc910, SMSG_ARENA_TEAM_EVENT 006cc980, 0x359 00741b60, 0x35a 00741b60, SMSG_ARENA_TEAM_STATS 005a2d50, SMSG_LFG_LFR_LIST 0055b770, SMSG_LFG_PROPOSAL_UPDATE 0055bdc0, SMSG_LFG_ROLE_CHECK_UPDATE 0055bdc0, SMSG_LFG_JOIN_RESULT 0055bdc0, SMSG_LFG_QUEUE_STATUS 0055bdc0, SMSG_LFG_UPDATE_PLAYER 0055bdc0, SMSG_LFG_UPDATE_PARTY 0055bdc0, SMSG_LFG_UPDATE_SEARCH 0055bdc0, SMSG_LFG_BOOT_PROPOSAL_UPDATE 0055bdc0, SMSG_LFG_PLAYER_INFO 0055bdc0, SMSG_LFG_PARTY_INFO 0055bdc0, SMSG_TITLE_EARNED 0050c520, SMSG_ARENA_ERROR 006cce90, 0x377 005e7b00, SMSG_DEATH_RELEASE_LOC 006e2e90, SMSG_FORCED_DEATH_UPDATE 006e2e90, 0x37e 00741b00, 0x380 00741b00, SMSG_FORCE_FLIGHT_SPEED_CHANGE 00732450, SMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE 00732450, SMSG_MOVE_SPLINE_SET_FLIGHT_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_FLIGHT_BACK_SPEED 00741bc0, SMSG_FLIGHT_SPLINE_SYNC 00716940, SMSG_OFFER_PETITION_ERROR 005cea00, SMSG_TIME_SYNC_REQUEST 006dc010, SMSG_REAL_GROUP_UPDATE 006cc300, SMSG_LFG_DISABLED 0055bdc0, SMSG_CHEAT_DUMP_ITEMS_DEBUG_ONLY_RESPONSE 006354d0, SMSG_UPDATE_COMBO_POINTS 00526530, SMSG_VOICE_SESSION_ROSTER_UPDATE 006ccf10, SMSG_VOICE_SESSION_LEAVE 006cd1b0, SMSG_VOICE_SET_TALKER_MUTED 006cd210, 0x3a7 00741b60, SMSG_IGNORE_REQUIREMENTS_CHEAT 006e2e90, SMSG_DISMOUNT 00741a40, 0x3ad 00741b60, 0x3ae 005744f0, SMSG_VOICE_PARENTAL_CONTROLS 006cfef0, SMSG_GM_MESSAGECHAT 0050ebc0, SMSG_COMMENTATOR_STATE_CHANGED 0056b8a0, SMSG_COMMENTATOR_MAP_INFO 0056bf30, SMSG_COMMENTATOR_PLAYER_INFO 0056b280, SMSG_CLEAR_TARGET 00756800, SMSG_CROSSED_INEBRIATION_THRESHOLD 006d01b0, SMSG_COMPLAINT_RESULT 006e2e90, SMSG_FEATURE_SYSTEM_STATUS 006e2e90, SMSG_CHANNEL_MEMBER_COUNT 004fb540, SMSG_AVAILABLE_VOICE_CHANNEL 006cd0e0, 0x3df 005725c0, SMSG_VOICE_CHAT_STATUS 00500240, SMSG_REPORT_PVP_AFK_RESULT 006e2e90, SMSG_GUILD_BANK_QUERY_RESULTS 005a7250, 0x3ee 005a4800, SMSG_USERLIST_ADD 00504130, SMSG_USERLIST_REMOVE 005042f0, SMSG_USERLIST_UPDATE 00500380, SMSG_INSPECT_TALENT 006ce0c0, SMSG_ECHO_PARTY_SQUELCH 00572610, SMSG_LOOT_LIST 0071ca50, 0x3fd 005cb9f0, 0x3fe 005a4ab0, 0x3ff 005ca6a0, SMSG_MIRROR_IMAGE_COMPONENTED_DATA 007324b0, SMSG_FORCE_DISPLAY_UPDATE 00716cd0, SMSG_IGNORE_DIMINISHING_RETURNS_CHEAT 006e2e90, 0x40a 005a4ae0, SMSG_OVERRIDE_LIGHT 00526530, SMSG_TOTEM_CREATED 00526530, SMSG_SEND_UNLEARN_SPELLS 006e2240, SMSG_PROPOSE_LEVEL_GRANT 00526530, SMSG_REFER_A_FRIEND_FAILURE 00526530, SMSG_MOVE_SPLINE_SET_FLYING 00741c30, SMSG_MOVE_SPLINE_UNSET_FLYING 00741c30, SMSG_SUMMON_CANCEL 006cbcf0, SMSG_ENABLE_BARBER_SHOP 0052f9b0, SMSG_BARBER_SHOP_RESULT 0052e5b0, SMSG_CALENDAR_SEND_CALENDAR 005c3fe0, SMSG_CALENDAR_SEND_EVENT 005c3fe0, SMSG_CALENDAR_FILTER_GUILD 005c3fe0, SMSG_CALENDAR_ARENA_TEAM 005c3fe0, SMSG_CALENDAR_EVENT_INVITE 005c3fe0, SMSG_CALENDAR_EVENT_INVITE_REMOVED 005c3fe0, SMSG_CALENDAR_EVENT_STATUS 005c3fe0, SMSG_CALENDAR_COMMAND_RESULT 006cd380, SMSG_CALENDAR_RAID_LOCKOUT_ADDED 005c3fe0, SMSG_CALENDAR_RAID_LOCKOUT_REMOVED 005c3fe0, SMSG_CALENDAR_EVENT_INVITE_ALERT 005c3fe0, SMSG_CALENDAR_EVENT_INVITE_REMOVED_ALERT 005c3fe0, SMSG_CALENDAR_EVENT_INVITE_STATUS_ALERT 005c3fe0, SMSG_CALENDAR_EVENT_REMOVED_ALERT 005c3fe0, SMSG_CALENDAR_EVENT_UPDATED_ALERT 005c3fe0, SMSG_CALENDAR_EVENT_MODERATOR_STATUS_ALERT 005c3fe0, SMSG_CALENDAR_SEND_NUM_PENDING 005c3fe0, SMSG_NOTIFY_DANCE 00576730, SMSG_PLAY_DANCE 00575ab0, SMSG_STOP_DANCE 00575850, SMSG_DANCE_QUERY_RESPONSE 00635ab0, SMSG_INVALIDATE_DANCE 006354f0, SMSG_LEARNED_DANCE_MOVES 005758a0, 0x45b 00741b00, SMSG_FORCE_PITCH_RATE_CHANGE 00732450, SMSG_MOVE_SPLINE_SET_PITCH_RATE 00741bc0, SMSG_CALENDAR_EVENT_INVITE_NOTES 005c3fe0, SMSG_CALENDAR_EVENT_INVITE_NOTES_ALERT 005c3fe0, SMSG_UPDATE_ACCOUNT_DATA_COMPLETE 006b8fc0, SMSG_TRIGGER_MOVIE 00526530, SMSG_ACHIEVEMENT_EARNED 005b3020, SMSG_CRITERIA_UPDATE 005b3160, SMSG_RESPOND_INSPECT_ACHIEVEMENTS 005b34a0, SMSG_QUEST_UPDATE_ADD_PVP_CREDIT 006d8030, SMSG_CALENDAR_RAID_LOCKOUT_UPDATED 005c3fe0, SMSG_PET_RENAMEABLE 005d3140, SMSG_PHASE_SHIFT_CHANGE 00526530, SMSG_ALL_ACHIEVEMENT_DATA 005b32f0, SMSG_HEALTH_UPDATE 00716d20, SMSG_POWER_UPDATE 007236c0, SMSG_HIGHEST_THREAT_UPDATE 00741c90, SMSG_THREAT_UPDATE 00741c90, SMSG_THREAT_REMOVE 00737b20, SMSG_THREAT_CLEAR 00734b00, SMSG_CONVERT_RUNE 006e2e90, SMSG_RESYNC_RUNES 006e2e90, SMSG_ADD_RUNE_POWER 006e2e90, SMSG_NOTIFY_DEST_LOC_SPELL_CAST 00810050, SMSG_AUCTION_LIST_PENDING_SALES 0059e880, SMSG_MODIFY_COOLDOWN 00804010, SMSG_PET_UPDATE_COMBO_POINTS 005d36a0, SMSG_PRE_RESSURECT 00716d80, SMSG_SERVER_FIRST_ACHIEVEMENT 0050b010, SMSG_PET_LEARNED_SPELLS 005d4c30, SMSG_PET_UNLEARNED_SPELLS 005d4c30, SMSG_CRITERIA_DELETED 005b36f0, SMSG_ACHIEVEMENT_DELETED 005b3610, SMSG_PLAYER_VEHICLE_DATA 00716db0, SMSG_PET_GUIDS 005d6550, SMSG_ITEM_REFUND_INFO_RESPONSE 006defa0, SMSG_ITEM_PURCHASE_REFUND_RESULT 006defa0, SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE 00526530, 0x4bb 005c3fe0, SMSG_LOAD_EQUIPMENT_SET 005af490, 0x4bf 008005a0, 0x4c0 006cd770, 0x4c7 0054b5e0, 0x4c8 006e2e90, 0x4cd 00714ad0, 0x4ce 00732450, 0x4d0 00732450, 0x4d2 00741b60, 0x4d3 00741c30, 0x4d4 00741c30, 0x4d6 005af710, 0x4d8 00714b20, 0x4da 004d92d0, 0x4de 0054b610, 0x4e0 005498c0, 0x4e1 0054b7a0, 0x4e4 0054b680, 0x4e5 0054b750, 0x4e6 005499c0, 0x4e8 0054b7f0, 0x4ed 006e2e90, 0x4ee 005ad240, 0x4ef 005ad240, 0x4f1 005ad240, 0x4f7 00526530, 0x4fa 00526530, 0x4fc 008c8de0, 0x4fd 006d84f0, 0x501 005b5190, 0x506 00526530, 0x50a 006e2e90, 0x50b 006e6330, 0x514 006d4110, 0x515 005528d0, 0x516 00732450, 0x518 00741b00, 0x51c 0056bb70, 0x51d 00568420, 0x51e 00716af0

## CVar coverage (CVar::Register)

The reference registers 426 cvars by literal name; frozen registers 426 of them. Missing ones are settings the reference client honours and this one cannot even store.

Missing: 

## Coverage by reference module

Module = the source file named by the reference's own assert strings near the function (linker order); `?` = no anchor before it.

| module | ref fns | bytes | mapped | bytes | stub | verified | spine |
|---|---:|---:|---:|---:|---:|---:|---:|
| DBClient.cpp | 1262 | 274.2k | 4 (0.3%) | 0.2% | 0 | 0 | 82 |
| OggDecompress.cpp | 1504 | 260.7k | 15 (1.0%) | 0.9% | 2 | 0 | 476 |
| ComSatSoundIOSoundEngine.cpp | 975 | 189.6k | 58 (5.9%) | 0.2% | 0 | 0 | 107 |
| Unit_C.cpp | 705 | 182.3k | 11 (1.6%) | 1.0% | 0 | 0 | 294 |
| Player_C.cpp | 736 | 148.3k | 12 (1.6%) | 5.9% | 0 | 0 | 163 |
| HealthBar.cpp | 448 | 102.9k | 8 (1.8%) | 3.6% | 0 | 0 | 68 |
| M2Scene.cpp | 283 | 101.1k | 87 (30.7%) | 47.9% | 8 | 0 | 210 |
| CGxDeviceD3d9Ex.cpp | 238 | 98.4k | 15 (6.3%) | 8.3% | 0 | 0 | 1 |
| GameUI.cpp | 491 | 96.4k | 199 (40.5%) | 45.2% | 92 | 0 | 73 |
| Spell_C.cpp | 355 | 86.2k | 14 (3.9%) | 5.7% | 0 | 0 | 121 |
| Tooltip.cpp | 151 | 86.0k | 75 (49.7%) | 62.9% | 38 | 1 | 16 |
| ChatFrame.cpp | 349 | 82.2k | 89 (25.5%) | 14.1% | 50 | 2 | 36 |
| lmemPool.cpp | 342 | 80.2k | 79 (23.1%) | 32.1% | 0 | 0 | 107 |
| Map.cpp | 237 | 77.5k | 3 (1.3%) | 3.3% | 0 | 0 | 143 |
| SpellCast.cpp | 261 | 70.6k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| WorldParam.cpp | 187 | 64.3k | 7 (3.7%) | 3.5% | 3 | 0 | 118 |
| fmod_systemi.cpp | 222 | 62.1k | 0 (0.0%) | 0.0% | 0 | 0 | 99 |
| InputControl.cpp | 287 | 61.8k | 30 (10.5%) | 18.3% | 11 | 0 | 115 |
| SoundEngine.cpp | 406 | 59.7k | 15 (3.7%) | 17.5% | 0 | 0 | 58 |
| SEvt.cpp | 242 | 59.1k | 32 (13.2%) | 5.5% | 8 | 0 | 111 |
| CreepTendril.cpp | 1415 | 58.5k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| SpellBookFrame.cpp | 246 | 57.0k | 41 (16.7%) | 13.2% | 22 | 0 | 15 |
| FFXEffects.cpp | 219 | 56.2k | 16 (7.3%) | 8.2% | 2 | 0 | 72 |
| PartyFrame.cpp | 304 | 55.3k | 76 (25.0%) | 33.0% | 56 | 0 | 11 |
| CSimpleAnimScript.cpp | 223 | 51.5k | 11 (4.9%) | 5.3% | 0 | 0 | 8 |
| Minigame_C.cpp | 352 | 50.8k | 193 (54.8%) | 43.3% | 0 | 0 | 68 |
| LFGInfo.cpp | 229 | 47.7k | 29 (12.7%) | 13.0% | 10 | 0 | 7 |
| ScriptEvents.cpp | 225 | 46.9k | 167 (74.2%) | 71.8% | 30 | 0 | 26 |
| CSimpleHyperlinkedFrame.cpp | 198 | 46.2k | 48 (24.2%) | 20.8% | 0 | 0 | 120 |
| M2Shared.cpp | 65 | 45.9k | 10 (15.4%) | 7.7% | 1 | 0 | 19 |
| ScanDLLGlue.cpp | 184 | 45.1k | 11 (6.0%) | 5.5% | 3 | 0 | 41 |
| CSimpleHTML.cpp | 364 | 44.4k | 208 (57.1%) | 63.2% | 7 | 2 | 0 |
| fmod_codec_it.cpp | 57 | 42.9k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| asiolist.cpp | 172 | 42.5k | 0 (0.0%) | 0.0% | 0 | 0 | 4 |
| DetailDoodad.cpp | 162 | 41.3k | 0 (0.0%) | 0.0% | 0 | 0 | 71 |
| VehicleCamera_C.cpp | 95 | 40.7k | 1 (1.1%) | 0.4% | 0 | 0 | 39 |
| TextureCache.cpp | 252 | 38.9k | 17 (6.7%) | 12.6% | 6 | 0 | 79 |
| TradeSkillFrame.cpp | 163 | 38.8k | 3 (1.8%) | 0.5% | 3 | 0 | 23 |
| framing.c | 125 | 38.8k | 34 (27.2%) | 52.4% | 0 | 0 | 39 |
| DBCache.cpp | 236 | 38.8k | 30 (12.7%) | 10.7% | 0 | 0 | 83 |
| GameObject_C.cpp | 285 | 38.0k | 4 (1.4%) | 1.1% | 0 | 0 | 59 |
| GxuFontMiscClasses.cpp | 156 | 37.5k | 6 (3.8%) | 10.2% | 0 | 0 | 86 |
| MapChunk.cpp | 129 | 37.2k | 23 (17.8%) | 22.9% | 0 | 0 | 85 |
| ConsoleVar.cpp | 243 | 36.1k | 73 (30.0%) | 29.8% | 2 | 0 | 68 |
| CSimpleFrameScript.cpp | 242 | 35.7k | 206 (85.1%) | 88.9% | 10 | 1 | 4 |
| TextureBlob.cpp | 214 | 34.6k | 26 (12.1%) | 16.2% | 0 | 0 | 84 |
| AchievementInfo.cpp | 179 | 33.7k | 62 (34.6%) | 32.0% | 60 | 0 | 0 |
| Calendar.cpp | 109 | 32.4k | 18 (16.5%) | 16.3% | 18 | 0 | 0 |
| fmod_output_openal.cpp | 57 | 31.2k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| UnitMissileTrajectory_C.cpp | 97 | 31.1k | 0 (0.0%) | 0.0% | 0 | 0 | 61 |
| MinimapFrame.cpp | 78 | 31.1k | 21 (26.9%) | 17.9% | 0 | 0 | 3 |
| XMLTree.cpp | 184 | 31.1k | 78 (42.4%) | 47.6% | 11 | 0 | 31 |
| MapChunkLiquid.cpp | 77 | 30.9k | 0 (0.0%) | 0.0% | 0 | 0 | 47 |
| Client.cpp | 169 | 30.8k | 53 (31.4%) | 38.0% | 10 | 0 | 31 |
| CGlueMgr.cpp | 183 | 28.9k | 92 (50.3%) | 55.8% | 31 | 0 | 1 |
| LoadingScreen.cpp | 201 | 28.7k | 8 (4.0%) | 10.0% | 0 | 0 | 80 |
| fmod_sample_software.cpp | 51 | 28.1k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| CScriptRegion.cpp | 204 | 27.4k | 110 (53.9%) | 58.1% | 2 | 4 | 50 |
| UIMacros.cpp | 139 | 27.2k | 4 (2.9%) | 2.4% | 4 | 0 | 6 |
| CSimpleRender.cpp | 166 | 26.6k | 36 (21.7%) | 33.5% | 2 | 0 | 50 |
| UnitCombatLog_C.cpp | 106 | 26.2k | 1 (0.9%) | 2.1% | 0 | 0 | 33 |
| CSimpleFrame.cpp | 144 | 25.9k | 26 (18.1%) | 26.1% | 2 | 0 | 36 |
| GossipInfo.cpp | 173 | 25.3k | 21 (12.1%) | 9.8% | 11 | 0 | 4 |
| SoundInterface2Internal.cpp | 139 | 25.2k | 15 (10.8%) | 17.3% | 3 | 0 | 13 |
| PaperDollInfoFrame.cpp | 111 | 25.0k | 28 (25.2%) | 26.4% | 12 | 0 | 8 |
| CSimpleAnim.cpp | 141 | 24.4k | 51 (36.2%) | 58.7% | 2 | 2 | 7 |
| TalentInfo.cpp | 141 | 24.1k | 3 (2.1%) | 1.8% | 3 | 0 | 1 |
| MovementShared.cpp | 85 | 23.5k | 1 (1.2%) | 1.1% | 0 | 0 | 42 |
| fmod_channel_openal.cpp | 52 | 23.2k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| PetNameCache.cpp | 120 | 22.8k | 0 (0.0%) | 0.0% | 0 | 0 | 38 |
| BattlefieldInfo.cpp | 120 | 22.7k | 48 (40.0%) | 44.1% | 24 | 0 | 0 |
| Texture.cpp | 146 | 22.2k | 24 (16.4%) | 24.0% | 5 | 0 | 75 |
| MapMem.cpp | 101 | 21.3k | 1 (1.0%) | 1.7% | 0 | 0 | 63 |
| Cursor.cpp | 117 | 20.7k | 3 (2.6%) | 4.5% | 0 | 0 | 19 |
| ObjectEffect.cpp | 81 | 20.5k | 0 (0.0%) | 0.0% | 0 | 0 | 33 |
| FriendList.cpp | 92 | 20.5k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| CSimpleEditBox.cpp | 94 | 20.4k | 17 (18.1%) | 20.9% | 0 | 0 | 0 |
| CSimpleMovieFrame.cpp | 128 | 19.6k | 46 (35.9%) | 34.9% | 5 | 1 | 2 |
| CheckExecutableSignature.cpp | 95 | 19.5k | 3 (3.2%) | 1.9% | 0 | 0 | 29 |
| BattlenetLogin.cpp | 111 | 19.4k | 1 (0.9%) | 0.0% | 0 | 0 | 0 |
| TaxiMapFrame.cpp | 97 | 19.4k | 8 (8.2%) | 9.3% | 8 | 0 | 2 |
| Profile.cpp | 128 | 19.1k | 7 (5.5%) | 4.6% | 0 | 0 | 28 |
| DressUpModelFrame.cpp | 116 | 18.8k | 12 (10.3%) | 13.5% | 8 | 0 | 7 |
| AddOns.cpp | 101 | 18.1k | 1 (1.0%) | 0.2% | 0 | 0 | 7 |
| KnowledgeBase.cpp | 119 | 17.8k | 1 (0.8%) | 0.1% | 0 | 0 | 0 |
| MailInfo.cpp | 77 | 17.8k | 8 (10.4%) | 3.6% | 3 | 0 | 7 |
| DeclinedWords.cpp | 72 | 17.4k | 7 (9.7%) | 20.8% | 4 | 0 | 32 |
| OsClipboard.cpp | 74 | 16.8k | 14 (18.9%) | 25.0% | 0 | 0 | 45 |
| QuestTextParser.cpp | 87 | 16.5k | 9 (10.3%) | 3.3% | 1 | 0 | 33 |
| Item_C.cpp | 107 | 16.4k | 1 (0.9%) | 0.4% | 0 | 0 | 24 |
| QuestLog.cpp | 80 | 16.4k | 10 (12.5%) | 12.9% | 7 | 0 | 2 |
| CGxDevice.cpp | 54 | 16.2k | 8 (14.8%) | 12.5% | 0 | 0 | 9 |
| tga.cpp | 82 | 16.2k | 1 (1.2%) | 0.8% | 0 | 0 | 23 |
| AuctionHouse.cpp | 49 | 16.1k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| fmod_codec_dls.cpp | 36 | 16.0k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| Grunt.cpp | 69 | 15.9k | 2 (2.9%) | 6.4% | 0 | 0 | 5 |
| fmod_plugin.cpp | 39 | 15.7k | 0 (0.0%) | 0.0% | 0 | 0 | 15 |
| ComSatClient.cpp | 86 | 15.6k | 6 (7.0%) | 4.2% | 6 | 0 | 6 |
| SComp.cpp | 83 | 15.1k | 1 (1.2%) | 1.2% | 1 | 0 | 2 |
| PetInfo.cpp | 69 | 14.8k | 5 (7.2%) | 5.3% | 4 | 0 | 7 |
| ActionBarFrame.cpp | 64 | 14.7k | 18 (28.1%) | 19.9% | 6 | 0 | 8 |
| PlayerName.cpp | 71 | 14.6k | 5 (7.0%) | 3.5% | 0 | 0 | 7 |
| UIMacroOptions.cpp | 93 | 14.4k | 1 (1.1%) | 1.6% | 0 | 0 | 0 |
| fmod_dsp_pitchshift.cpp | 52 | 14.4k | 0 (0.0%) | 0.0% | 0 | 0 | 5 |
| fmod_codec_wav_riff.cpp | 46 | 14.1k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| GxuFontUtil.cpp | 60 | 13.3k | 2 (3.3%) | 1.0% | 0 | 0 | 45 |
| EffectGlow.cpp | 64 | 13.1k | 2 (3.1%) | 6.6% | 0 | 0 | 5 |
| blp.cpp | 94 | 13.0k | 21 (22.3%) | 12.5% | 0 | 0 | 20 |
| fmod_output_wasapi.cpp | 68 | 12.8k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| ObjectMgrClient.cpp | 89 | 12.8k | 9 (10.1%) | 15.3% | 0 | 0 | 23 |
| VehiclePassenger_C.cpp | 49 | 12.7k | 0 (0.0%) | 0.0% | 0 | 0 | 17 |
| fmod_dsp_echo.cpp | 55 | 12.5k | 0 (0.0%) | 0.0% | 0 | 0 | 8 |
| GuildBankFrame.cpp | 71 | 12.4k | 9 (12.7%) | 8.8% | 1 | 0 | 13 |
| fmod_os_cdda.cpp | 30 | 12.4k | 0 (0.0%) | 0.0% | 0 | 0 | 14 |
| SLock.cpp | 68 | 12.3k | 1 (1.5%) | 2.0% | 0 | 1 | 18 |
| CGxD3d9ExTexture.cpp | 47 | 12.2k | 2 (4.3%) | 3.8% | 0 | 0 | 5 |
| CharacterCreation.cpp | 56 | 12.2k | 24 (42.9%) | 51.2% | 4 | 0 | 3 |
| aSfxDsp.cpp | 48 | 12.1k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| EvtSched.cpp | 73 | 12.1k | 2 (2.7%) | 6.9% | 0 | 0 | 33 |
| SoundInterface2ZoneSounds.cpp | 68 | 11.6k | 0 (0.0%) | 0.0% | 0 | 0 | 23 |
| UIBindings.cpp | 59 | 11.5k | 21 (35.6%) | 44.3% | 13 | 0 | 4 |
| CSimpleMessageScrollFrame.cpp | 75 | 11.4k | 8 (10.7%) | 11.6% | 0 | 0 | 0 |
| fmod_codec_mpeg.cpp | 43 | 11.3k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| ContainerFrame.cpp | 32 | 11.2k | 9 (28.1%) | 26.5% | 0 | 0 | 0 |
| EquipmentManager.cpp | 55 | 10.8k | 1 (1.8%) | 0.3% | 1 | 0 | 3 |
| ConsoleClient.cpp | 52 | 10.3k | 11 (21.2%) | 18.0% | 0 | 0 | 6 |
| SBig.cpp | 54 | 10.2k | 0 (0.0%) | 0.0% | 0 | 0 | 36 |
| WowConnection.cpp | 44 | 10.1k | 1 (2.3%) | 2.3% | 0 | 0 | 4 |
| RaidInfo.cpp | 33 | 10.1k | 17 (51.5%) | 43.5% | 10 | 0 | 1 |
| CurrencyTypes.cpp | 48 | 10.1k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| MapLoad.cpp | 29 | 9.9k | 1 (3.4%) | 3.3% | 0 | 0 | 22 |
| fmod_output_dsound.cpp | 36 | 9.9k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| fmod_codec_s3m.cpp | 14 | 9.7k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| fmod_codec.cpp | 43 | 9.2k | 0 (0.0%) | 0.0% | 0 | 0 | 9 |
| fmod_codec_fsb.cpp | 18 | 9.2k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| WardenClient.cpp | 62 | 8.9k | 3 (4.8%) | 11.8% | 2 | 0 | 0 |
| TradeFrame.cpp | 43 | 8.8k | 7 (16.3%) | 24.3% | 7 | 0 | 5 |
| vorbisfile.c | 23 | 8.6k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| DanceStudio.cpp | 53 | 8.6k | 0 (0.0%) | 0.0% | 0 | 0 | 24 |
| DuelInfo.cpp | 49 | 8.4k | 1 (2.0%) | 0.4% | 1 | 0 | 4 |
| AccountData.cpp | 47 | 8.4k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| GuildInfo.cpp | 35 | 8.3k | 2 (5.7%) | 16.8% | 1 | 0 | 1 |
| MerchantFrame.cpp | 41 | 8.3k | 10 (24.4%) | 32.0% | 6 | 0 | 4 |
| fmod_pluginfactory.cpp | 38 | 8.3k | 0 (0.0%) | 0.0% | 0 | 0 | 23 |
| MapObjRead.cpp | 37 | 8.1k | 0 (0.0%) | 0.0% | 0 | 0 | 11 |
| fmod_codec_oggvorbis.cpp | 31 | 7.9k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| CalendarEvent.cpp | 40 | 7.9k | 1 (2.5%) | 3.4% | 0 | 0 | 0 |
| ChatBubbleFrame.cpp | 55 | 7.6k | 0 (0.0%) | 0.0% | 0 | 0 | 9 |
| RealmList.cpp | 46 | 7.5k | 26 (56.5%) | 54.0% | 2 | 0 | 0 |
| fmod_sample_openal.cpp | 19 | 7.4k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| PetitionVendor.cpp | 38 | 6.9k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| Vehicle_C.cpp | 43 | 6.8k | 0 (0.0%) | 0.0% | 0 | 0 | 19 |
| Corpse_C.cpp | 63 | 6.8k | 2 (3.2%) | 4.2% | 0 | 0 | 14 |
| cmemblock.cpp | 46 | 6.7k | 25 (54.3%) | 52.9% | 6 | 0 | 16 |
| SSignature.cpp | 36 | 6.7k | 6 (16.7%) | 11.8% | 0 | 0 | 18 |
| SCmd.cpp | 55 | 6.6k | 5 (9.1%) | 1.3% | 0 | 0 | 16 |
| fmod_dsp_itecho.cpp | 35 | 6.6k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| UnitCombat_C.cpp | 25 | 6.5k | 0 (0.0%) | 0.0% | 0 | 0 | 9 |
| hidmanagerimpl.cpp | 34 | 6.4k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| ItemSocketInfo.cpp | 56 | 6.1k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| ClientServices.cpp | 37 | 6.1k | 1 (2.7%) | 0.6% | 1 | 0 | 0 |
| EZ_LCD_Page.cpp | 69 | 6.1k | 0 (0.0%) | 0.0% | 0 | 0 | 4 |
| UnitSound_C.cpp | 37 | 6.0k | 0 (0.0%) | 0.0% | 0 | 0 | 13 |
| UnitVehicle_C.cpp | 36 | 6.0k | 0 (0.0%) | 0.0% | 0 | 0 | 21 |
| NetClient.cpp | 44 | 5.9k | 2 (4.5%) | 3.4% | 0 | 0 | 6 |
| LootFrame.cpp | 36 | 5.8k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| block.c | 16 | 5.8k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| EZ_LCD.cpp | 54 | 5.7k | 0 (0.0%) | 0.0% | 0 | 0 | 8 |
| NamePlateFrame.cpp | 10 | 5.6k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| OsURLDownload.cpp | 21 | 5.5k | 0 (0.0%) | 0.0% | 0 | 0 | 5 |
| GMTicketInfo.cpp | 37 | 5.3k | 11 (29.7%) | 22.6% | 10 | 0 | 1 |
| fmod.cpp | 110 | 5.2k | 1 (0.9%) | 0.9% | 0 | 0 | 42 |
| fmod_codec_mod.cpp | 10 | 5.0k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| fmod_thread.cpp | 25 | 4.9k | 0 (0.0%) | 0.0% | 0 | 0 | 16 |
| fmod_dsp_sfxreverb.cpp | 30 | 4.8k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| fmod_file.cpp | 44 | 4.8k | 0 (0.0%) | 0.0% | 0 | 0 | 17 |
| fmod_file_net.cpp | 8 | 4.7k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| ClassTrainerFrame.cpp | 18 | 4.7k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| WDataStore.cpp | 33 | 4.6k | 0 (0.0%) | 0.0% | 0 | 0 | 15 |
| ObjectAlloc.cpp | 41 | 4.6k | 3 (7.3%) | 4.1% | 0 | 0 | 20 |
| GruntLogin.cpp | 21 | 4.6k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| Path.cpp | 41 | 4.6k | 1 (2.4%) | 1.7% | 0 | 0 | 18 |
| LootRoll.cpp | 30 | 4.6k | 0 (0.0%) | 0.0% | 0 | 0 | 9 |
| CGxDeviceOpenGl.cpp | 25 | 4.5k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| Bag_C.cpp | 34 | 4.5k | 1 (2.9%) | 3.2% | 0 | 0 | 12 |
| MapArea.cpp | 21 | 4.5k | 0 (0.0%) | 0.0% | 0 | 0 | 11 |
| ReputationInfo.cpp | 40 | 4.5k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| fmod_codec_tag.cpp | 7 | 4.4k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| ArenaTeamInfo.cpp | 33 | 4.3k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| CSimpleMessageFrame.cpp | 29 | 4.2k | 2 (6.9%) | 3.5% | 0 | 0 | 0 |
| OsTcp.cpp | 23 | 4.2k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| ShaderEffectManager.cpp | 24 | 4.1k | 2 (8.3%) | 24.5% | 0 | 0 | 3 |
| OsVersionHash.cpp | 19 | 4.1k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| CharacterComponent.cpp | 11 | 4.0k | 1 (9.1%) | 28.8% | 0 | 0 | 5 |
| RCString.cpp | 46 | 4.0k | 4 (8.7%) | 8.8% | 0 | 0 | 17 |
| OsSecureRandom.cpp | 33 | 3.9k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| fmod_string.cpp | 18 | 3.9k | 0 (0.0%) | 0.0% | 0 | 0 | 14 |
| fmod_output.cpp | 20 | 3.9k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| SoundInterface2VoiceChat.cpp | 44 | 3.9k | 0 (0.0%) | 0.0% | 0 | 0 | 13 |
| CSimpleFont.cpp | 25 | 3.8k | 7 (28.0%) | 54.5% | 0 | 0 | 1 |
| TumorManager.cpp | 44 | 3.8k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| CGxDeviceD3d.cpp | 15 | 3.8k | 1 (6.7%) | 5.1% | 0 | 0 | 0 |
| fmod_output_software.cpp | 14 | 3.7k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| DynamicObject_C.cpp | 25 | 3.6k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| ModelBlob.cpp | 29 | 3.5k | 0 (0.0%) | 0.0% | 0 | 0 | 4 |
| Tumor.cpp | 58 | 3.5k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| SThread.cpp | 17 | 3.4k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| TimeManager.cpp | 34 | 3.3k | 1 (2.9%) | 1.0% | 0 | 0 | 5 |
| fmod_output_asio.cpp | 22 | 3.2k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| fmod_channelpool.cpp | 20 | 3.2k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| mdct.c | 11 | 2.8k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| BattlenetUI.cpp | 31 | 2.8k | 2 (6.5%) | 5.4% | 0 | 0 | 6 |
| StableInfo.cpp | 15 | 2.7k | 1 (6.7%) | 1.3% | 1 | 0 | 0 |
| fmod_output_wavwriter_nrt.cpp | 23 | 2.7k | 0 (0.0%) | 0.0% | 0 | 0 | 4 |
| sharedbook.c | 8 | 2.7k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| fmod_codec_flac.cpp | 22 | 2.6k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| floor1.c | 7 | 2.6k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| PetitionInfo.cpp | 20 | 2.5k | 1 (5.0%) | 21.3% | 1 | 0 | 0 |
| ItemStats.cpp | 3 | 2.4k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| fmod_file_cdda.cpp | 12 | 2.3k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| Trade_C.cpp | 13 | 2.3k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| SurveyDownloadGlue.cpp | 10 | 2.2k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| res0.c | 8 | 2.2k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| SkillInfo.cpp | 16 | 2.2k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| Login.cpp | 16 | 2.2k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| fmod_dsp_connectionpool.cpp | 9 | 2.2k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| fmod_dsp_resampler.cpp | 8 | 2.0k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| envelope.c | 6 | 2.0k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| codebook.c | 8 | 2.0k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| Lightning.cpp | 7 | 2.0k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| CharacterModelBase.cpp | 24 | 2.0k | 6 (25.0%) | 28.4% | 5 | 0 | 0 |
| floor0.c | 13 | 2.0k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| ? | 15 | 0.8k | 6 (40.0%) | 58.1% | 1 | 0 | 7 |

## Next to port: unmapped, on the world spine (by callers x size)

| addr | module | size | callers | frozen | strings |
|---|---|---:|---:|---|---|
| 00509dd0 | ChatFrame.cpp | 4047 | 126 |  | !@#$%^&*, %d. %s |
| 00808200 | Spell_C.cpp | 2446 | 98 |  | %s_PET, .\Spell_C.cpp |
| 007385c0 | Unit_C.cpp | 4083 | 57 |  | .\Unit_C.cpp |
| 0061fec0 | Tooltip.cpp | 756 | 204 | examined -- see overrides.json | %sTextLeft%d, %sTextRight%d |
| 0067ca30 | DBCache.cpp | 412 | 275 | examined -- see overrides.json | .?AUDBCACHECALLBACK@@ |
| 00745230 | Unit_C.cpp? | 2889 | 29 |  |  |
| 006f61d0 | ObjectEffect.cpp | 579 | 125 |  |  |
| 0072a000 | Unit_C.cpp | 651 | 69 |  | .\Unit_C.cpp, UNKNOWNOBJECT |
| 005dd5a0 | TradeSkillFrame.cpp | 2894 | 14 |  | .PBVSkillLineAbilityRec@@, .\TradeSkillFrame.cpp |
| 00519280 | GameUI.cpp | 513 | 83 |  | .\GameUI.cpp, INTERFACESOUND_CURSORDROPOBJECT |
| 008d67d0 | fmod_systemi.cpp | 7647 | 4 | examined -- see overrides.json | ..\..\src\fmod_systemi.cpp, TITLE |
| 005fbbc0 | InputControl.cpp | 588 | 53 |  | .\InputControl.cpp |
| 00729740 | Unit_C.cpp | 815 | 37 |  |  |
| 007413f0 | Unit_C.cpp | 695 | 41 |  |  |
| 00771d10 | SSignature.cpp? | 2374 | 11 |  | 
%s

,  : error %u:  |
| 0067de90 | DBCache.cpp | 412 | 67 |  | .?AUDBCACHECALLBACK@@ |
| 0080cce0 | Spell_C.cpp | 3395 | 7 |  | .\Spell_C.cpp, GAMEABILITYACTIVATE |
| 00736d30 | Unit_C.cpp | 923 | 24 |  | .\Unit_C.cpp |
| 0053cf10 | SpellBookFrame.cpp | 646 | 34 |  | d:\BuildServer\WoW\1\work\WoW-code\branc |
| 007251c0 | Unit_C.cpp | 798 | 27 |  |  |
| 0041d770 | OggDecompress.cpp | 199 | 103 |  |  |
| 00708c20 | Item_C.cpp | 1998 | 9 |  | .\Item_C.cpp, SPELL_FAILED_NO_CHARGES_REMAIN |
| 008cfda0 | fmod_memory.cpp | 388 | 48 |  | ..\..\src\fmod_memory.cpp |
| 00802f80 | Spell_C.cpp | 1396 | 12 |  | .\Spell_C.cpp |
| 004922f0 | CSimpleFrame.cpp | 951 | 18 |  |  |
| 00603330 | InputControl.cpp? | 1379 | 12 |  | d:\BuildServer\WoW\1\work\WoW-code\branc |
| 0080bc80 | Spell_C.cpp | 1723 | 9 |  | .\Spell_C.cpp |
| 004102b2 | LoadingScreen.cpp? | 2420 | 6 | examined -- see overrides.json | (null) |
| 00786e10 | SEvt.cpp? | 2403 | 6 |  |  |
| 00859160 | lmemPool.cpp? | 5573 | 2 |  | 'for' initial value must be a number, 'for' limit must be a number |
| 0095d110 | CDataAllocator.cpp | 156 | 106 |  | .\CDataAllocator.cpp |
| 0080ac90 | Spell_C.cpp | 2357 | 6 |  | %s%s%s%s, .\Spell_C.cpp |
| 007e11d0 | DeclinedWords.cpp? | 1074 | 14 |  |  |
| 0097be80 | CSimpleHyperlinkedFrame.cpp? | 5350 | 2 |  |  |
| 00735f60 | Unit_C.cpp | 1750 | 8 |  | .\Unit_C.cpp |
| 00524bf0 | GameUI.cpp | 965 | 15 |  | .\GameUI.cpp, d:\BuildServer\WoW\1\work\WoW-code\branc |
| 004ee460 | ScanDLLGlue.cpp? | 569 | 26 |  |  |
| 00735820 | Unit_C.cpp | 567 | 26 |  | .\Unit_C.cpp |
| 0080b5d0 | Spell_C.cpp | 1700 | 8 |  | .\Spell_C.cpp |
| 00484470 | CSimpleRender.cpp | 374 | 37 |  |  |

## Next to port: unmapped, anywhere

| addr | module | size | callers | frozen | strings |
|---|---|---:|---:|---|---|
| 00509dd0 | ChatFrame.cpp | 4047 | 126 |  | !@#$%^&*, %d. %s |
| 00808200 | Spell_C.cpp | 2446 | 98 |  | %s_PET, .\Spell_C.cpp |
| 007385c0 | Unit_C.cpp | 4083 | 57 |  | .\Unit_C.cpp |
| 006918f0 | CGxDeviceD3d9Ex.cpp? | 1531 | 113 |  |  |
| 0061fec0 | Tooltip.cpp | 756 | 204 | examined -- see overrides.json | %sTextLeft%d, %sTextRight%d |
| 006238a0 | Tooltip.cpp | 6714 | 19 |  | %d-%d, %s (%d) |
| 0067ca30 | DBCache.cpp | 412 | 275 | examined -- see overrides.json | .?AUDBCACHECALLBACK@@ |
| 00745230 | Unit_C.cpp? | 2889 | 29 |  |  |
| 006f61d0 | ObjectEffect.cpp | 579 | 125 |  |  |
| 00695fd0 | CGxDeviceD3d9Ex.cpp? | 24484 | 1 |  |  |
| 0072a000 | Unit_C.cpp | 651 | 69 |  | .\Unit_C.cpp, UNKNOWNOBJECT |
| 0093cd10 | fmod_output_dsound.cpp? | 2731 | 15 |  |  |
| 005dd5a0 | TradeSkillFrame.cpp | 2894 | 14 |  | .PBVSkillLineAbilityRec@@, .\TradeSkillFrame.cpp |
| 00519280 | GameUI.cpp | 513 | 83 |  | .\GameUI.cpp, INTERFACESOUND_CURSORDROPOBJECT |
| 00546310 | SpellBookFrame.cpp? | 1486 | 25 |  | d:\BuildServer\WoW\1\work\WoW-code\branc |
| 008d67d0 | fmod_systemi.cpp | 7647 | 4 | examined -- see overrides.json | ..\..\src\fmod_systemi.cpp, TITLE |
| 005fbbc0 | InputControl.cpp | 588 | 53 |  | .\InputControl.cpp |
| 00729740 | Unit_C.cpp | 815 | 37 |  |  |
| 009411a0 | fmod_sample_software.cpp? | 7717 | 3 |  |  |
| 009b1b40 | SpellCast.cpp? | 5858 | 4 |  | CDATA, ENTITIES |
| 007413f0 | Unit_C.cpp | 695 | 41 |  |  |
| 00771d10 | SSignature.cpp? | 2374 | 11 |  | 
%s

,  : error %u:  |
| 0067de90 | DBCache.cpp | 412 | 67 |  | .?AUDBCACHECALLBACK@@ |
| 009013b0 | fmod_codec_it.cpp | 9335 | 2 |  | ..\..\src\fmod_codec_it.cpp, FMOD IT final mixdown unit |
| 0080cce0 | Spell_C.cpp | 3395 | 7 |  | .\Spell_C.cpp, GAMEABILITYACTIVATE |
| 008a65e0 | ComSatSoundIOSoundEngine.cpp? | 6621 | 3 |  |  |
| 009567a0 | asiolist.cpp? | 374 | 67 |  |  |
| 0093f9b0 | fmod_sample_software.cpp? | 6100 | 3 |  |  |
| 00492770 | CSimpleFrame.cpp | 1133 | 20 |  |  |
| 0075f9d0 | VehicleCamera_C.cpp? | 1465 | 15 |  |  |
| 008fe980 | fmod_codec_it.cpp | 7704 | 2 |  |  |
| 00736d30 | Unit_C.cpp | 923 | 24 |  | .\Unit_C.cpp |
| 00547170 | SpellBookFrame.cpp? | 2287 | 9 |  | %s Inside Instance:  Continent = %02d  M, %s, Dungeon Map Info (continent):  WMOID |
| 0053cf10 | SpellBookFrame.cpp | 646 | 34 |  | d:\BuildServer\WoW\1\work\WoW-code\branc |
| 007251c0 | Unit_C.cpp | 798 | 27 |  |  |
| 006a3c40 | CGxDeviceD3d9Ex.cpp? | 873 | 24 |  |  |
| 006a7be0 | CGxD3d9ExTexture.cpp? | 873 | 24 |  |  |
| 0093e470 | fmod_sample_software.cpp? | 5405 | 3 |  |  |
| 0041d770 | OggDecompress.cpp | 199 | 103 |  |  |
| 00708c20 | Item_C.cpp | 1998 | 9 |  | .\Item_C.cpp, SPELL_FAILED_NO_CHARGES_REMAIN |

## Mapped but stubbed (WHOA_UNIMPLEMENTED)

| addr | module | size | callers | frozen | strings |
|---|---|---:|---:|---|---|
| 00820ae0 | M2Scene.cpp? | 1109 | 1 | `CM2SceneRender::DrawBatchDoodad` [override] | BatchDoodad: model=%s index=%d |
| 00779ae0 | SComp.cpp? | 183 | 10 | `MD5Final` [callorder] |  |
| 00820720 | M2Scene.cpp? | 957 | 1 | `CM2SceneRender::DrawBatchProj` [override] | BatchProj: model=%s index=%d |
| 00837680 | M2Shared.cpp | 957 | 1 | `CM2Shared::SubstituteSpecializedShaders` [override] |  |
| 005e95c0 | PaperDollInfoFrame.cpp | 1501 | 0 | `Script_GetInventoryItemsForSlot` [table] | .\PaperDollInfoFrame.cpp, Usage: GetInventoryItemsForSlot(slot [,  |
| 00488540 | CSimpleRender.cpp? | 42 | 33 | `CScriptRegion::ProtectedFunctionsAllowed` [override] |  |
| 00828f90 | M2Scene.cpp? | 464 | 2 | `CM2Model::SetIndices` [annotated] |  |
| 0050f990 | ChatFrame.cpp? | 1255 | 0 | `Script_SetConsoleKey` [table] | BACKSPACE, DECIMAL |
| 005bd8a0 | Calendar.cpp | 1253 | 0 | `Script_Stub_CalendarGetEventInfo` [table] |  |
| 005104a0 | ChatFrame.cpp? | 1143 | 0 | `Script_SetCursor` [table] | ATTACK_CURSOR, ATTACK_ERROR_CURSOR |
| 005c1070 | Calendar.cpp | 1029 | 0 | `Script_Stub_CalendarGetDayEvent` [table] | Usage: CalendarGetDayEvent([-1.0.1], mon |
| 00573690 | RaidInfo.cpp | 975 | 0 | `Script_GetRaidRosterInfo` [table] | .\RaidInfo.cpp, MAINASSIST |
| 00494d20 | CSimpleFrame.cpp? | 311 | 2 | `CFrameStrata::FrameOccluded` [override] |  |
| 00825d70 | M2Scene.cpp? | 144 | 5 | `CM2Model::UnoptimizeVisibleGeometry` [callgraph] |  |
| 004b95b0 | Texture.cpp | 427 | 1 | `CreateTgaTexture` [override] | .\Texture.cpp, Error loading file "%s": Texture size mu |
| 00515200 | GameUI.cpp | 808 | 0 | `Script_GetCursorInfo` [table] | .\GameUI.cpp, CRITTER |
| 008214e0 | M2Scene.cpp? | 391 | 1 | `CM2SceneRender::DrawParticle` [override] | Particle: model=%s |
| 004b8a50 | Texture.cpp | 388 | 1 | `CreateBlpAsync` [override] | HTEXTURE |
| 005564d0 | LFGInfo.cpp | 763 | 0 | `Script_Stub_GetLFDChoiceCollapseState` [order] | Usage: GetLFDChoiceCollapseState([table] |
| 0051cdb0 | GameUI.cpp | 754 | 0 | `Script_EquipItemByName` [table] | EquipItemByName(): Invalid inventory dst, d:\BuildServer\WoW\1\work\WoW-code\branc |
| 00537240 | PartyFrame.cpp? | 719 | 0 | `Script_BNGetFOFInfo` [table] | BNUI: BNGetFOFInfo for ID %u index %d is, Incorrect ID |
| 00599b20 | DressUpModelFrame.cpp? | 677 | 0 | `CGTabardModelFrame_GetLowerEmblemTexture` [table] | %s:GetLowerEmblemTexture(): Couldn't fin, %s:GetLowerEmblemTexture(): Wrong object |
| 0054be90 | BattlefieldInfo.cpp | 656 | 0 | `Script_GetBattlefieldScore` [table] | Usage: GetBattlefieldScore(index) |
| 005879d0 | TradeFrame.cpp | 656 | 0 | `Script_ClickTradeButton` [table] | .\TradeFrame.cpp, Usage: ClickTradeButton(index) |
| 00599890 | DressUpModelFrame.cpp? | 646 | 0 | `CGTabardModelFrame_GetUpperEmblemTexture` [table] | %s:GetUpperEmblemTexture(): Couldn't fin, %s:GetUpperEmblemTexture(): Wrong object |
| 0054da10 | BattlefieldInfo.cpp | 641 | 0 | `Script_AcceptBattlefieldPort` [table] | Usage: AcceptBattlefieldPort(index, acce, d:\BuildServer\WoW\1\work\WoW-code\branc |
| 0062eae0 | Tooltip.cpp | 640 | 0 | `CGTooltip_SetTradeSkillItem` [table] | Invalid trade skill item in SetTradeSkil |
| 0062fcf0 | Tooltip.cpp | 621 | 0 | `CGTooltip_SetAuctionItem` [table] | Usage: %s:SetAuctionItem("type", index), bidder |
| 00495060 | CSimpleFrame.cpp? | 308 | 1 | `CSimpleTop::CompressStrata` [callorder] |  |
| 00584e10 | MerchantFrame.cpp | 608 | 0 | `Script_Stub_GetMerchantItemInfo` [order] | %s%s%s, Usage: GetMerchantItemInfo(index) |
| 005e8030 | QuestLog.cpp? | 596 | 0 | `Script_GetInspectArenaTeamData` [table] | Usage: GetInspectArenaTeamData(index) |
| 00587c60 | TradeFrame.cpp | 589 | 0 | `Script_GetTradeTargetItemInfo` [table] | %s%s%s, Usage: GetTradeTargetItemInfo(index) |
| 0052e1b0 | PartyFrame.cpp | 578 | 0 | `Script_SetPartyAssignment` [table] | .\PartyFrame.cpp, Invalid Party assignment |
| 0051c450 | GameUI.cpp | 574 | 0 | `Script_IsUsableItem` [table] | d:\BuildServer\WoW\1\work\WoW-code\branc |
| 004f5720 | TextureCache.cpp? | 287 | 1 | `MirrorInitialize` [callorder] |  |
| 0062f1e0 | Tooltip.cpp | 571 | 0 | `CGTooltip_SetTradeTargetItem` [table] | Invalid trade slot in SetTradeTargetItem |
| 0062f9e0 | Tooltip.cpp | 569 | 0 | `CGTooltip_SetInboxItem` [table] | Usage: %s:SetInboxItem(messageIndex, att |
| 0053a300 | PartyFrame.cpp? | 568 | 0 | `Script_BNListConversation` [table] | %s%s%s, PLAYER_LIST_DELIMITER |
| 0049edb0 | CSimpleFrameScript.cpp | 552 | 0 | `CSimpleFrame_HookScript` [table] | %s doesn't have a "%s" script, Usage: %s:HookScript("type", function) |
| 004a5df0 | CSimpleFrameScript.cpp? | 552 | 0 | `CSimpleAnim_HookScript` [table] | %s doesn't have a "%s" script, Usage: %s:HookScript("type", function) |

## Divergence smells: reference strings the frozen counterpart never mentions

A reference function that formats, asserts or looks up a string its port does not is missing a branch, an error path or a data lookup. Top 40 by count.

| addr | frozen | missing |
|---|---|---|
| 006277f0 | `TooltipSetItemInfo` | 			<setspelldesc_%d>; 			<setspelldesc_%d></setspelldesc_%d>\n; 			<spelldesc_%d>;  (%s) |
| 00877aa0 | `FMOD_ErrorString` | A CDDA read error occurred. ; A HTTP error occurred. This is a catch-all for HTT; A HTTP server error occurred. ; A Win32 COM related error occured. COM failed to i |
| 0087c710 | `SESound::Init` |  - %d Channels Requested.;  - %d Output drivers detected;  - DSPBufferSize = %d [Valid values are 0 = AUTO D;  - DSPBufferSize = AUTO DETECT |
| 005104a0 | `Script_SetCursor` | ATTACK_CURSOR; ATTACK_ERROR_CURSOR; BUY_CURSOR; BUY_ERROR_CURSOR |
| 004d1600 | `SI2::RegisterUserCVars` |  - ========= PLAYBACK =========;  - ========== VOLUME ==========;  - =========== MISC ===========;  - Ambience Volume       [%.2f] |
| 0051d9b0 | `CGGameUI::RegisterGameCVars` | Automatically loot items when the loot window open; Clear the target when clicking on terrain; Enables the equipment management UI; How long to display Battle.net toast windows, in s |
| 0050f990 | `Script_SetConsoleKey` | BACKSPACE; DECIMAL; DELETE; DIVIDE |
| 004dab40 | `CGlueMgr::PollAccountLogin` | %d%d%d%d%b%d; %d%d%d%d%d%d%d%d%d%d; %s\n%s; CHANGE_REALM |
| 004067f0 | `InitializeGlobal` | .PAD; Database compression; Desired method for game timing; Error reported by the timing validation system |
| 00621070 | `TooltipUnitLevelLine` |  - %s; %s - %s; %s: %s; FACTION_STANDING_LABEL%d |
| 00405ab0 | `BuildPatchArchiveList` | ..\Data\; ..\Data\%s\; Data\; Data\%s\ |
| 0079e7c0 | `CMap::MapMemInitialize` | CMap::lowDetailIndexPool; Shaders\Vertex; Terrain; Terrain0 |
| 0061d3d0 | `CGTooltip_SetAnchorType` | ANCHOR_BOTTOM; ANCHOR_BOTTOMLEFT; ANCHOR_BOTTOMRIGHT; ANCHOR_CURSOR |
| 00405dd0 | `Sub405DD0` | Country; Data\; Failed to open archive %s.; Failed to read data from the network. Please check |
| 00403910 | `TransferAbortedHandler` | TRANSFER_ABORT_DIFFICULTY%d; TRANSFER_ABORT_ERROR; TRANSFER_ABORT_INSUF_EXPAN_LVL%d; TRANSFER_ABORT_MAP_NOT_ALLOWED |
| 006e2e90 | `InventoryChangeFailureHandler` | %d%d%d%d%d%d%d%d%d; %s%s%s%s%s%s; COMPLAINT_ADDED; Godmode disabled |
| 0061eb40 | `CGTooltip_SetOwner` | ANCHOR_BOTTOM; ANCHOR_BOTTOMLEFT; ANCHOR_BOTTOMRIGHT; ANCHOR_CURSOR |
| 0061d650 | `CGTooltip_GetAnchorType` | ANCHOR_BOTTOM; ANCHOR_BOTTOMLEFT; ANCHOR_BOTTOMRIGHT; ANCHOR_CURSOR |
| 008ce200 | `FindPatchPrefix_SC2_ArchiveName` | <unknown>; Grunt; My public address is %s; My realm ID is %d |
| 0060abf0 | `Script_GetTokenFromGUID` | arena; arenapet; commentator; d:\BuildServer\WoW\1\work\WoW-code\branches\wow-pa |
| 00562ed0 | `UIBindingsSetKey` | APOSTROPHE; BACKSLASH; COMMA; LEFTBRACKET |
| 00515200 | `Script_GetCursorInfo` | CRITTER; MOUNT; UNKNOWN; companion |
| 007654a0 | `SysMsgPrintf` | %s %d %d %d; Invalid number of parameters; Make sure to specify the red, green and blue color; One or more colors are not in the 0 to 255 range o |
| 0049a060 | `CSimpleAnimGroup::LoadXML` | Alpha; Animation; Couldn't find inherited node: %s; Recursively inherited node: %s |
| 0060a630 | `Script_GetGUIDFromString` | arena%d; arenapet%d; d:\BuildServer\WoW\1\work\WoW-code\branches\wow-pa; party%d |
| 00832ea0 | `CM2Model::InitializeLoaded` | "%s", %s = %g; "%s", %s = %g, %s = %g; shared->farClip; shared->fieldOfView |
| 005a8f10 | `Script_GetActionInfo` | CRITTER; MOUNT; UNKNOWN; Usage: GetActionInfo(slot) |
| 0051ba50 | `Script_GetZonePVPInfo` | arena; combat; contested; d:\BuildServer\WoW\1\work\WoW-code\branches\wow-pa |
| 0069ed50 | `CGxDeviceGLL::PatchVertexShader` |  = program.env  ;  = program.local;  = { program.env  ;  = { program.local |
| 00631000 | `CGTooltip_SetAction` | ATTACK; PET_ACTION_%s; PET_MODE_%s; UberTooltips |
| 005b8570 | `Script_Stub_CalendarEventSortInvites` | Usage: CalendarEventSortInvites("criteria", revers; class; level; notes |
| 0054de00 | `Script_SortBattlefieldScoreData` | Usgae: SortBattlefieldScoreData("type"); class; damage; deaths |
| 00537240 | `Script_BNGetFOFInfo` | BNUI: BNGetFOFInfo for ID %u index %d is ID %u, Ac; Incorrect ID; Is not; Must select mutual and/or non. |
| 00536e40 | `Script_BNReportPlayer` | ABUSE; BNET_REPORT_SENT; Report note is too long.; THREAT |
| 00535180 | `Script_BNGetFriendInviteInfo` | BNUI: Invite Info Account name: %s %s; BNUI: Invite Info ID: %u; BNUI: Invite Info message: %s; BNUI: Invite Info time: %d |
| 005343f0 | `Script_BNGetInfo` | BNUI: GetInfo AFK is %d; BNUI: GetInfo DND is %d; BNUI: GetInfo account ID is %u; BNUI: GetInfo custom message is %s |
| 0052e1b0 | `Script_SetPartyAssignment` | Invalid Party assignment; MAINASSIST; MAINTANK; SetPartyAssignment |
| 0052a980 | `WowClientInit` | Cannot create WOWClient log file "%s"!; GameTooltip; Interface\FrameXML\Bindings.xml; Interface\FrameXML\FrameXML.toc |
| 004a7e00 | `CSimpleAnimGroup_CreateAnimation` | %s:CreateAnimation(): Couldn't find inherited node; %s:CreateAnimation(): Recursively inherited node "; Alpha; Rotation |
| 0081f330 | `CM2SceneRender::CM2SceneRender` | Particle; Particle_Unlit; Projected_ModAdd; Projected_ModMod |

## Largest frozen functions with no reference link

Either the port added behaviour the reference does not have, or the link is simply unknown: tag it with `// ref: FUN_xxxxxxxx` once found.

| frozen | lib | code bytes | file |
|---|---|---:|---|
| `LoadWmoInstance` | world | 10517 | src/world/Terrain.cpp |
| `GetPredAdvancedBy0x1` | m4vh263dec | 8257 | vendor/m4vh263dec/src/get_pred_adv_b_add.cpp |
| `CFF_Parse_CharStrings` | lib/freetype-2.0 | 7964 | vendor/freetype-2.0.9/src/cff/cffgload.c |
| `doProlog` | lib/expat-2.0 | 7308 | lib/common/vendor/expat-2.0.1/lib/xmlparse.c |
| `ParticleFxRenderImpl` | world | 7220 | src/world/ParticleFx.cpp |
| `GetPredAdvancedBy1x0` | m4vh263dec | 5923 | vendor/m4vh263dec/src/get_pred_adv_b_add.cpp |
| `std::_Matcher3<wchar_t,std::regex_traits<wchar_t>,wchar_t const *,void>::_Match_pat` | glue | 4820 |  |
| `SHA1_Transform` | lib/common | 4679 | lib/common/common/SHA1.cpp |
| `ParseLegacyLiquid` | world | 4522 | src/world/Terrain.cpp |
| `M2Init` | model | 4489 | src/model/M2Init.cpp |
| `GetPredOutside` | m4vh263dec | 4486 | vendor/m4vh263dec/src/get_pred_outside.cpp |
| `T1_Decoder_Parse_Charstrings` | lib/freetype-2.0 | 4304 | vendor/freetype-2.0.9/src/psaux/t1decode.c |
| `ParseChunk` | world | 4052 | src/world/Terrain.cpp |
| `doContent` | lib/expat-2.0 | 3900 | lib/common/vendor/expat-2.0.1/lib/xmlparse.c |
| `CWorld::UpdateOutdoorLight` | world | 3841 | src/world/CWorld.cpp |
| `LzmaDec_DecodeReal` | lib/stormlib-9.31 | 3776 | lib/squall/vendor/stormlib-9.31/src/lzma/c/lzmadec.c |
| `DecodeVOLHeader` | m4vh263dec | 3581 | vendor/m4vh263dec/src/vop.cpp |
| `LoadTile` | world | 3560 | src/world/Terrain.cpp |
| `Rebuild` | object | 3317 | src/object/client/SpellBook.cpp |
| `VlcDequantH263IntraBlock` | m4vh263dec | 3168 | vendor/m4vh263dec/src/vlc_dequant.cpp |
| `H263_Deblock` | m4vh263dec | 3153 | vendor/m4vh263dec/src/post_filter.cpp |
| `ParticleFxUpdateModel` | world | 3054 | src/world/ParticleFx.cpp |
| `ChrRacesRec::Read` | db | 2957 | src/db/rec/ChrRacesRec.cpp |
| `RenderWmos` | world | 2898 | src/world/Terrain.cpp |
| `MapRec::Read` | db | 2780 | src/db/rec/MapRec.cpp |
| `GetPredAdvancedBy1x1` | m4vh263dec | 2737 | vendor/m4vh263dec/src/get_pred_adv_b_add.cpp |
| `TerrainUpdateView` | world | 2685 | src/world/Terrain.cpp |
| `ParseLiquid` | world | 2616 | src/world/Terrain.cpp |
| `SFileOpenArchive` | lib/stormlib-9.31 | 2611 | lib/squall/vendor/stormlib-9.31/src/SFileOpenArchive.cpp |
| `AchievementRec::Read` | db | 2604 | src/db/rec/AchievementRec.cpp |
| `storeAtts` | lib/expat-2.0 | 2588 | lib/common/vendor/expat-2.0.1/lib/xmlparse.c |
| `ChrClassesRec::Read` | db | 2561 | src/db/rec/ChrClassesRec.cpp |
| `luaK_posfix` | lib/lua-5.1 | 2528 | vendor/lua-5.1.3/src/lcode.c |
| `CGxString::CreateGeometry` | gx | 2492 | src/gx/font/CGxString.cpp |
| `CBackdropGenerator::SetOutput` | ui | 2401 | src/ui/CBackdropGenerator.cpp |
| `FreeTile` | world | 2372 | src/world/Terrain.cpp |
| `normal_prologTok` | lib/expat-2.0 | 2360 | lib/common/vendor/expat-2.0.1/lib/xmltok_impl.c |
| `DecodeShortHeader` | m4vh263dec | 2324 | vendor/m4vh263dec/src/vop.cpp |
| `BuildSkyDome` | world | 2303 | src/world/Terrain.cpp |
| `TT_CharMap_Load` | lib/freetype-2.0 | 2236 | vendor/freetype-2.0.9/src/sfnt/ttcmap.c |

## Linked ports with the lowest call-order fidelity

The port exists but does not make the calls the reference makes, in the order it makes them. Either the port guessed, or its callees are not yet linked (then `--show` lists them as bare addresses). Non-stub, largest first.

| addr | frozen | call order | ref calls | frozen calls | ref branches | frozen branches | consts | size |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 006277f0 | `TooltipSetItemInfo` | 23% | 604 | 54 | 797 | 37 | 4% | 24814 |
| 0051d9b0 | `CGGameUI::RegisterGameCVars` | 97% | 192 | 230 | 4 | 0 | 40% | 6776 |
| 0082f0f0 | `CM2Model::AnimateMT` | 16% | 47 | 42 | 152 | 52 | 2% | 6267 |
| 0087c710 | `SESound::Init` | 11% | 230 | 41 | 83 | 5 | 2% | 6078 |
| 00832ea0 | `CM2Model::InitializeLoaded` | 29% | 71 | 36 | 170 | 61 | 2% | 5663 |
| 00821a20 | `CM2Scene::Animate` | 29% | 54 | 35 | 219 | 45 | 8% | 5621 |
| 00857ca0 | `luaV_execute` | 47% | 50 | 48 | 163 | 115 | 2% | 5138 |
| 00621070 | `TooltipUnitLevelLine` | 20% | 137 | 33 | 172 | 19 | 2% | 5010 |
| 006e2e90 | `InventoryChangeFailureHandler` | 7% | 214 | 14 | 104 | 8 | 2% | 4848 |
| 004dab40 | `CGlueMgr::PollAccountLogin` | 17% | 148 | 29 | 90 | 15 | 10% | 3696 |
| 00823130 | `CM2SceneRender::Draw` | 81% | 25 | 28 | 39 | 12 | 0% | 2909 |
| 00526530 | `ReceiveWeather` | 3% | 115 | 7 | 40 | 3 | 0% | 2495 |
| 006d8870 | `ReceiveGroupList` | 34% | 77 | 37 | 80 | 11 | 2% | 2482 |
| 004d1600 | `SI2::RegisterUserCVars` | 29% | 79 | 23 | 25 | 0 | 0% | 2232 |
| 0079e7c0 | `CMap::MapMemInitialize` | 60% | 45 | 26 | 21 | 0 | 16% | 2068 |
| 004e3cd0 | `CCharacterSelection::ShowCharacter` | 35% | 37 | 7 | 49 | 5 | 0% | 2058 |
| 004c6a40 | `SI2::PlaySoundKit` | 11% | 48 | 28 | 74 | 24 | 17% | 1787 |
| 00405dd0 | `Sub405DD0` | 22% | 50 | 2 | 59 | 0 | 0% | 1706 |
| 004fa5f0 | `CGWorldFrame::OnWorldUpdate` | 14% | 52 | 32 | 27 | 7 | 0% | 1493 |
| 007e18c0 | `ValidateNameInitialize` | 0% | 10 | 4 | 90 | 4 | 0% | 1475 |
| 00828a00 | `CM2Model::AnimateST` | 50% | 24 | 8 | 40 | 17 | 0% | 1415 |
| 0087b5f0 | `SEDiskSound::CompleteNonBlockingLoad` | 12% | 33 | 9 | 37 | 5 | 0% | 1395 |
| 0062dae0 | `CGTooltip_SetHyperlink` | 31% | 66 | 26 | 41 | 12 | 0% | 1382 |
| 00631000 | `CGTooltip_SetAction` | 14% | 51 | 7 | 43 | 2 | 0% | 1377 |
| 0078e400 | `CWorldParam::Initialize` | 84% | 37 | 34 | 4 | 0 | 0% | 1354 |
| 0049a060 | `CSimpleAnimGroup::LoadXML` | 49% | 57 | 19 | 40 | 8 | 4% | 1310 |
| 0081fe90 | `CM2SceneRender::SetupMaterial` | 62% | 13 | 16 | 41 | 14 | 4% | 1306 |
| 0069ed50 | `CGxDeviceGLL::PatchVertexShader` | 50% | 10 | 9 | 66 | ? | ? | 1299 |
| 0052a980 | `WowClientInit` | 16% | 59 | 23 | 23 | 2 | 0% | 1267 |
| 0062e050 | `CGTooltip_SetInventoryItem` | 21% | 39 | 14 | 31 | 6 | 0% | 1226 |
| 004f1a20 | `CCharacterComponent::Initialize` | 40% | 10 | 11 | 10 | 9 | 29% | 1189 |
| 00479860 | `fallbackSort` | 10% | 10 | 10 | 45 | 34 | 26% | 1182 |
| 0096fed0 | `CSimpleButton::LoadXML` | 72% | 46 | 49 | 34 | 23 | 0% | 1174 |
| 005cde20 | `Script_GetSkillLineInfo` | 21% | 63 | 13 | 23 | 0 | 4% | 1173 |
| 00686120 | `CGxDevice::IRsInit` | 50% | 4 | 174 | 3 | 0 | 2% | 1165 |
| 00497070 | `CSimpleFont::LoadXML` | 92% | 36 | 49 | 50 | 23 | 0% | 1155 |
| 00837a40 | `CM2Shared::InitializeSkinProfile` | 60% | 10 | 6 | 38 | 17 | 7% | 1137 |
| 0060a630 | `Script_GetGUIDFromString` | 14% | 44 | 13 | 55 | 12 | 8% | 1122 |
| 0060abf0 | `Script_GetTokenFromGUID` | 71% | 42 | 1 | 34 | 3 | 0% | 1121 |
| 00813ee0 | `FrameXML_ProcessFile` | 79% | 42 | 46 | 35 | 24 | 19% | 1104 |

## Runtime: last call trace (tools/recomp/calltrace.py + tracecompare.py)

Traced 2026-09-18 10:28: 3 frames on each client, frame-level call order agreement 58%, 18 functions verified (same per-frame count), 25 links contradicted (table below).

| reference calls every frame, frozen never (hits) | frozen calls, reference never (hits) |
|---|---|
| `ISStrVPrintf` 25 | `FrameScript_Execute` 60 |
| `luaV_tostring` 21 | `CSimpleFontString_GetText` 21 |
| `CSimpleButton::Enable` 15 | `FrameScript_Object::GetDisplayName` 3 |
| `SFile::IsStreamingTrial` 14 | `BlobShadowsBegin` 3 |
| `SI2::PlaySoundKit` 13 | `CSimpleTexture_SetTexCoord` 2 |
| `SStrChrR` 11 | `PostInitObject` 1 |
| `SESound::LoadDiskSound` 11 |  |
| `SEDiskSound::CompleteNonBlockingLoad` 11 |  |
| `luaC_step` 6 |  |
| `StringToBOOL` 6 |  |
| `Script_GetGUIDFromToken` 6 |  |
| `CVar::LookupRegistered` 6 |  |
| `CSimpleFrame_IsVisible` 6 |  |
| `luaL_checklstring` 3 |  |
| `Script_UnitMana` 3 |  |
| `Script_UnitHealth` 3 |  |
| `CSimpleFontString_SetText` 3 |  |
| `CSimpleFontString::SetText` 3 |  |
| `CScriptRegion_GetWidth` 3 |  |
| `CScriptRegion_GetHeight` 3 |  |

Per-frame count mismatches (ref \| frozen), largest first:

- `CDataStore::FetchRead` [595, 667, 1129] \| [355, 527, 521]
- `CM2Model::IsDrawable` [569, 568, 568] \| [871, 871, 871]
- `CM2Model::Sub825E00` [323, 418, 547] \| [90, 158, 142]
- `CM2Model::Sub826350` [152, 185, 227] \| [45, 79, 71]
- `CM2Model::Sub8260C0` [152, 183, 227] \| [45, 79, 71]
- `index2adr` [537, 545, 537] \| [613, 611, 613]
- `CM2Model::SetBoneSequence` [22, 35, 53] \| [0, 0, 1]
- `luaH_getnum` [134, 134, 134] \| [160, 160, 160]
- `lua_touserdata` [49, 50, 49] \| [64, 64, 64]
- `lua_insert` [68, 68, 68] \| [80, 80, 80]
- `CScriptObject_GetName` [1, 1, 1] \| [8, 8, 8]
- `lua_rawget` [34, 34, 34] \| [40, 40, 40]
- `NDCToDDC` [6, 6, 6] \| [3, 3, 3]
- `lua_gettop` [18, 18, 18] \| [21, 20, 21]
- `luaL_checknumber` [4, 4, 4] \| [3, 0, 3]

Links the trace contradicts -- a wrong link, or a real divergence; each needs a verdict in overrides.json (corrected link / `diverged` / `unlinked`):

| addr | frozen | link evidence | why | ref per frame | frozen per frame |
|---|---|---|---|---|---|
| 00422140 | `SFile::IsStreamingTrial` | callgraph | one side every frame, the other never | [7, 4, 3] | [0, 0, 0] |
| 00482aa0 | `FrameScript_Object::GetDisplayName` | override | one side every frame, the other never | [0, 0, 0] | [1, 1, 1] |
| 00483910 | `CSimpleFontString::SetText` | callorder | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 00488540 | `CSimpleButton::Enable` | override | one side every frame, the other never | [5, 5, 5] | [0, 0, 0] |
| 0048d730 | `CSimpleFontString_GetText` | table | one side every frame, the other never | [0, 0, 0] | [7, 7, 7] |
| 0048d780 | `CSimpleFontString_SetText` | table | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 0049d3b0 | `CScriptRegion_GetWidth` | table | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 0049d550 | `CScriptRegion_GetHeight` | table | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 0049fe30 | `CSimpleFrame_IsVisible` | table | one side every frame, the other never | [2, 2, 2] | [0, 0, 0] |
| 004c6a40 | `SI2::PlaySoundKit` | string | one side every frame, the other never | [6, 4, 3] | [0, 0, 0] |
| 004dab40 | `CGlueMgr::PollAccountLogin` | string | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 0060abf0 | `Script_GetGUIDFromToken` | annotated | one side every frame, the other never | [2, 2, 2] | [0, 0, 0] |
| 0060eb60 | `Script_UnitHealth` | table | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 0060ed40 | `Script_UnitMana` | annotated | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 00767460 | `CVar::LookupRegistered` | override | one side every frame, the other never | [2, 2, 2] | [0, 0, 0] |
| 0076e720 | `SStrChrR` | override | one side every frame, the other never | [6, 3, 2] | [0, 0, 0] |
| 0076f010 | `ISStrVPrintf` | callgraph | one side every frame, the other never | [13, 7, 5] | [0, 0, 0] |
| 007e4480 | `BlobShadowsBegin` | override | one side every frame, the other never | [0, 0, 0] | [1, 1, 1] |
| 008154e0 | `StringToBOOL` | override | one side every frame, the other never | [2, 2, 2] | [0, 0, 0] |
| 00819210 | `FrameScript_Execute` | unlinked | one side every frame, the other never | [0, 0, 0] | [20, 20, 20] |
| 0084f9f0 | `luaL_checklstring` | callgraph | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 00856ea0 | `luaV_tostring` | override | one side every frame, the other never | [7, 7, 7] | [0, 0, 0] |
| 0085b950 | `luaC_step` | override | one side every frame, the other never | [2, 2, 2] | [0, 0, 0] |
| 0087b5f0 | `SEDiskSound::CompleteNonBlockingLoad` | string | one side every frame, the other never | [6, 3, 2] | [0, 0, 0] |
| 0087ee60 | `SESound::LoadDiskSound` | string | one side every frame, the other never | [6, 3, 2] | [0, 0, 0] |

## Iterations

| run | linked | faithful | stub | spine linked | lua bindings | lua stubs |
|---|---:|---:|---:|---:|---:|---:|
| 2026-09-24 00:39 | 3141 (11.6%) | 1224 (4.5%) | 690 | 540/5529 | 2924/2964 | 1493 |
| 2026-09-24 00:44 | 3144 (11.6%) | 1227 (4.5%) | 690 | 543/5529 | 2924/2964 | 1493 |
| 2026-09-24 00:46 | 3146 (11.6%) | 1229 (4.5%) | 690 | 543/5529 | 2924/2964 | 1493 |
| 2026-09-24 00:52 | 3148 (11.6%) | 1229 (4.5%) | 690 | 545/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:02 | 3148 (11.6%) | 1229 (4.5%) | 690 | 545/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:07 | 3151 (11.6%) | 1230 (4.5%) | 690 | 548/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:15 | 3154 (11.6%) | 1231 (4.5%) | 690 | 551/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:20 | 3156 (11.6%) | 1233 (4.5%) | 690 | 553/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:27 | 3157 (11.6%) | 1234 (4.5%) | 690 | 553/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:34 | 3160 (11.6%) | 1236 (4.6%) | 690 | 556/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:41 | 3166 (11.7%) | 1240 (4.6%) | 690 | 557/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:47 | 3167 (11.7%) | 1240 (4.6%) | 690 | 557/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:50 | 3168 (11.7%) | 1240 (4.6%) | 690 | 557/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:55 | 3173 (11.7%) | 1243 (4.6%) | 690 | 558/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:55 | 3173 (11.7%) | 1243 (4.6%) | 690 | 558/5529 | 2924/2964 | 1493 |
| 2026-09-24 01:56 | 3173 (11.7%) | 1243 (4.6%) | 690 | 558/5529 | 2924/2964 | 1493 |
| 2026-09-24 02:02 | 3173 (11.7%) | 1243 (4.6%) | 690 | 558/5529 | 2924/2964 | 1493 |
| 2026-09-24 02:12 | 3177 (11.7%) | 1246 (4.6%) | 690 | 562/5529 | 2924/2964 | 1493 |
| 2026-09-24 02:20 | 3178 (11.7%) | 1247 (4.6%) | 690 | 563/5529 | 2924/2964 | 1493 |
| 2026-09-24 02:27 | 3179 (11.7%) | 1247 (4.6%) | 690 | 564/5529 | 2924/2964 | 1493 |
| 2026-09-24 02:30 | 3179 (11.7%) | 1247 (4.6%) | 690 | 564/5529 | 2924/2964 | 1493 |
| 2026-09-24 02:32 | 3180 (11.7%) | 1248 (4.6%) | 690 | 564/5529 | 2924/2964 | 1493 |
| 2026-09-24 02:37 | 3180 (11.7%) | 1248 (4.6%) | 690 | 564/5529 | 2924/2964 | 1493 |
| 2026-09-24 02:37 | 3180 (11.7%) | 1248 (4.6%) | 690 | 564/5529 | 2924/2964 | 1493 |
| 2026-09-24 02:39 | 3180 (11.7%) | 1248 (4.6%) | 690 | 564/5529 | 2924/2964 | 1493 |

## How to move a row

1. Pick the top unmapped spine function. `python tools/recomp/recomp.py --show <addr>` prints its callers, callees, strings and the closest frozen candidates; `C:\Users\tyler\tools\decomp.sh <out> <addr>` decompiles it.
2. Port it (or find the existing port) and put `// ref: FUN_<addr>` above the frozen definition.
3. When a run shows it behaving like the reference, add `{"<addr>": {"frozen": "<name>", "status": "verified", "note": "..."}}` to overrides.json.
4. Re-run the tool; the totals line shows the delta against the previous run.
