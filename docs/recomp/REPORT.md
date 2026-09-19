# Recomp map: reference 3.3.5a (12340) vs whoa

Generated 2026-09-18 23:05 by `tools/recomp/recomp.py`. Do not edit; put facts in `tools/recomp/overrides.json`
or `// ref: FUN_xxxxxxxx` tags above whoa definitions and re-run.

## Totals

| | functions | code bytes |
|---|---:|---:|
| reference (non-thunk) | 27161 | 5.41M |
| mapped to a whoa function | 1658 (+1) (6.1%) | 375.7k (6.8%) |
| &nbsp;&nbsp;ported | 1173 (+1) | 254.1k |
| &nbsp;&nbsp;stub (WHOA_UNIMPLEMENTED) | 463 (=) | 93.7k |
| &nbsp;&nbsp;verified (override) | 14 (=) | 3.0k |
| **faithful** (linked, not stub, call order >= 80%) | **329 (=) (1.2%)** | **45.7k (0.8%)** |
| unmapped | 25503 | 5.04M |
| world spine (reachable from OnFrameRender) | 5530, mapped 191 (+1) (3.5%) | |
| whoa functions (src/, from PDB + source) | 10653, stubs 895 | |

Match evidence: annotated 162, callgraph 71, callorder 87, cvar 30, handler 27, override 88, string 264, table 929. Module anchors: 1479 assert strings.

Previous run: 2026-09-18 23:01 -- mapped 1657, ported 1172, stub 463, spine mapped 190.

## Lua API coverage (binding tables)

The reference registers 2512 Lua bindings across 151 tables (widget methods per class, and the global function blocks). whoa registers 936 of them (=); 446 of those are WHOA_UNIMPLEMENTED stubs (=). A missing name is a FrameXML call that raises "attempt to call a nil value"; a stub returns nothing, which is the arity bug class tools/arity.py hunts.

| ref table | entries | whoa array | missing | stubbed | first missing / stubbed names |
|---|---:|---|---:|---:|---|
| 00ad21d8 (UnitExists..) | 168 | `s_UnitFunctions` | 0 | 63 |  / stubs: UnitPlayerOrPetInParty, UnitPlayerOrPetInRaid, UnitIsPVPSanctuary, UnitOnTaxi ... |
| 00ac7b70 (GetChatTypeIndex..) | 54 | `-` | 54 | 0 | GetChatTypeIndex, GetChatWindowInfo, GetChatWindowSavedPosition, GetChatWindowSavedDimensions, GetChatWindowMessages, GetChatWindowChannels ... |
| 00ad1060 (IsUnitOnQuest..) | 52 | `-` | 52 | 0 | IsUnitOnQuest, GetQuestLogQuestText, GetNumQuestLeaderBoards, GetQuestLogLeaderBoard, GetNumQuestItemDrops, GetQuestLogItemDrop ... |
| 00ad2ae0 (AddFontStrings..) | 69 | `CGTooltipMethods` | 0 | 51 |  / stubs: AddFontStrings, AddTexture, AppendText, FadeOut ... |
| 00b2d210 (SetFontObject..) | 58 | `SimpleEditBoxMethods` | 0 | 51 |  / stubs: SetFontObject, GetFontObject, SetFont, GetFont ... |
| 00accae8 (BNGetInfo..) | 57 | `s_ScriptFunctions` | 0 | 47 |  / stubs: BNGetInfo, BNGetFriendInfo, BNGetFriendInfoByID, BNGetNumFriendToons ... |
| 00ac1550 (GetTitleRegion..) | 38 | `-` | 38 | 0 | GetTitleRegion, CreateTitleRegion, CreateTexture, CreateFontString, GetBoundsRect, GetNumRegions ... |
| 00ad0840 (GetNumGuildMembers..) | 37 | `-` | 37 | 0 | GetNumGuildMembers, GetGuildRosterMOTD, GetGuildRosterInfo, GetGuildRosterLastOnline, GuildRosterSetPublicNote, GuildRosterSetOfficerNote ... |
| 00acd760 (GetLFGBootProposal..) | 36 | `-` | 36 | 0 | GetLFGBootProposal, SetLFGBootVote, GetLFGQueueStats, GetLastQueueStatusIndex, GetLFGDungeonInfo, GetLFGRandomDungeonInfo ... |
| 00ace620 (ClickSendMailItemButton..) | 36 | `-` | 36 | 0 | ClickSendMailItemButton, SetSendMailMoney, GetSendMailMoney, SetSendMailCOD, GetSendMailCOD, GetNumStationeries ... |
| 00ac7a58 (SendChatMessage..) | 34 | `-` | 34 | 0 | SendChatMessage, SendAddonMessage, SendSystemMessage, GetNumLanguages, GetLanguageByIndex, GetDefaultLanguage ... |
| 00ac8710 (GetMouseFocus..) | 34 | `-` | 34 | 0 | GetMouseFocus, GetRealmName, GetItemQualityColor, GetItemInfo, GetItemGem, GetExtendedItemInfo ... |
| 00b2cee0 (SetFontObject..) | 48 | `SimpleScrollingMessageFrameMethods` | 34 | 0 | SetFontObject, GetFontObject, SetFont, GetFont, SetTextColor, GetTextColor ... |
| 00ad0e88 (GetNumTradeSkills..) | 32 | `-` | 32 | 0 | GetNumTradeSkills, GetTradeSkillInfo, SelectTradeSkill, GetTradeSkillSelectionIndex, GetTradeSkillCooldown, GetTradeSkillIcon ... |
| 00ad93d8 (GetNumFriends..) | 31 | `-` | 31 | 0 | GetNumFriends, GetFriendInfo, SetSelectedFriend, GetSelectedFriend, AddOrRemoveFriend, AddFriend ... |
| 00ac8158 (GetFramerate..) | 30 | `-` | 30 | 0 | GetFramerate, TogglePerformanceDisplay, TogglePerformancePause, TogglePerformanceValues, ResetPerformanceValues, GetDebugStats ... |
| 00ac8460 (PickupPlayerMoney..) | 34 | `s_ScriptFunctions` | 0 | 29 |  / stubs: PickupPlayerMoney, HasSoulstone, UseSoulstone, GuildInvite ... |
| 00ac8858 (IsAddOnLoadOnDemand..) | 29 | `-` | 29 | 0 | IsAddOnLoadOnDemand, IsAddOnLoaded, LoadAddOn, PartialPlayTime, NoPlayTime, GetBillingTimeRested ... |
| 00acf8d0 (QueryGuildBankTab..) | 29 | `-` | 29 | 0 | QueryGuildBankTab, SetCurrentGuildBankTab, GetCurrentGuildBankTab, GetGuildBankItemInfo, SetGuildBankTabInfo, GetGuildBankItemLink ... |
| 00ac18f8 (IsDone..) | 28 | `-` | 28 | 0 | IsDone, IsPlaying, IsPaused, IsStopped, IsDelaying, GetElapsed ... |
| 00b2d068 (SetFontObject..) | 28 | `-` | 28 | 0 | SetFontObject, GetFontObject, SetFont, GetFont, SetTextColor, GetTextColor ... |
| 00acf3e8 (OpenTrainer..) | 27 | `-` | 27 | 0 | OpenTrainer, CloseTrainer, GetNumTrainerServices, GetTrainerServiceInfo, SelectTrainerService, IsTradeskillTrainer ... |
| 00acfcd0 (GetAchievementComparisonInfo..) | 25 | `-` | 25 | 0 | GetAchievementComparisonInfo, GetPreviousAchievement, GetNextAchievement, GetAchievementCategory, GetAchievementLink, GetNumCompletedAchievements ... |
| 00acde38 (GetNumBindings..) | 26 | `s_ScriptFunctions` | 0 | 24 |  / stubs: GetNumBindings, GetBinding, SetBinding, SetBindingSpell ... |
| 00acd488 (KBSetup_BeginLoading..) | 22 | `-` | 22 | 0 | KBSetup_BeginLoading, KBSetup_IsLoaded, KBSetup_GetLanguageCount, KBSetup_GetLanguageData, KBSetup_GetCategoryCount, KBSetup_GetCategoryData ... |
| 00ad0d50 (ContainerIDToInventoryID..) | 22 | `-` | 22 | 0 | ContainerIDToInventoryID, GetContainerNumSlots, GetContainerItemInfo, GetContainerItemID, GetContainerItemLink, GetContainerItemCooldown ... |
| 00ac1690 (IsVisible..) | 45 | `SimpleFrameMethods` | 0 | 21 |  / stubs: Lower, GetHitRectInsets, GetClampRectInsets, GetMinResize ... |
| 00acd248 (GetNumBattlefieldPositions..) | 25 | `s_ScriptFunctions` | 0 | 21 |  / stubs: GetNumBattlefieldPositions, GetBattlefieldPosition, GetNumBattlefieldFlagPositions, GetBattlefieldFlagPosition ... |
| 00acf678 (GetBidderAuctionItems..) | 20 | `-` | 20 | 0 | GetBidderAuctionItems, GetNumAuctionItems, GetAuctionItemInfo, GetAuctionItemLink, GetAuctionItemTimeLeft, PlaceAuctionBid ... |
| 00ad0160 (CalendarContextGetEventIndex..) | 20 | `-` | 20 | 0 | CalendarContextGetEventIndex, CalendarContextInviteIsPending, CalendarContextInviteModeratorStatus, CalendarContextInviteStatus, CalendarContextInviteType, CalendarContextInviteAvailable ... |
| 00ad0220 (CalendarEventSortInvites..) | 20 | `-` | 20 | 0 | CalendarEventSortInvites, CalendarEventGetInviteSortCriterion, CalendarEventGetStatusOptions, CalendarEventSetStatus, CalendarEventSetModerator, CalendarEventClearModerator ... |
| 00ad1270 (GetInventorySlotInfo..) | 24 | `s_ScriptFunctions` | 0 | 20 |  / stubs: GetInventoryItemsForSlot, GetInventoryItemTexture, GetInventoryItemBroken, GetInventoryItemCount ... |
| 00ac1ad0 (Finish..) | 23 | `ScriptObjectMethods` | 19 | 0 | Finish, GetProgress, IsDone, IsPlaying, IsPaused, IsPendingFinish ... |
| 00ac8278 (CursorHasItem..) | 19 | `-` | 19 | 0 | CursorHasItem, CursorHasSpell, CursorHasMacro, CursorHasMoney, GetCursorInfo, EquipCursorItem ... |
| 00accf58 (GetMapInfo..) | 19 | `-` | 19 | 0 | GetMapInfo, GetCurrentMapContinent, GetCurrentMapAreaID, GetCurrentMapZone, GetCurrentMapDungeonLevel, SetMapByID ... |
| 00ac1848 (SetFont..) | 18 | `-` | 18 | 0 | SetFont, GetFont, SetAlpha, GetAlpha, SetTextColor, GetTextColor ... |
| 00ace210 (CreateMacro..) | 18 | `-` | 18 | 0 | CreateMacro, GetNumMacros, GetMacroInfo, GetMacroBody, DeleteMacro, EditMacro ... |
| 00ac1168 (IsVisible..) | 31 | `SimpleFontStringMethods` | 0 | 17 |  / stubs: GetFieldSize, GetTextColor, GetShadowColor, SetShadowColor ... |
| 00ac80c8 (RegisterForSave..) | 17 | `-` | 17 | 0 | RegisterForSave, RegisterForSavePerCharacter, SetLayoutMode, IsModifierKeyDown, IsLeftShiftKeyDown, IsRightShiftKeyDown ... |
| 00ace400 (CommentatorSetCamera..) | 17 | `-` | 17 | 0 | CommentatorSetCamera, CommentatorGetCamera, CommentatorGetCurrentMapID, CommentatorStartInstance, CommentatorAddPlayer, CommentatorRemovePlayer ... |
| 00acee28 (SetLootPortrait..) | 17 | `-` | 17 | 0 | SetLootPortrait, GetNumLootItems, GetLootSlotInfo, GetLootSlotLink, LootSlotIsItem, LootSlotIsCoin ... |
| 00ad0030 (CalendarGetMonthNames..) | 17 | `-` | 17 | 0 | CalendarGetMonthNames, CalendarGetWeekdayNames, CalendarGetDate, CalendarGetMinDate, CalendarGetMaxDate, CalendarGetMinHistoryDate ... |
| 00ad05f0 (GetNumTalentTabs..) | 17 | `-` | 17 | 0 | GetNumTalentTabs, GetTalentTabInfo, GetNumTalents, GetTalentInfo, GetTalentLink, GetTalentPrereqs ... |
| 00acd178 (GetNumBattlefields..) | 16 | `-` | 16 | 0 | GetNumBattlefields, GetBattlefieldInfo, GetBattlefieldInstanceInfo, IsBattlefieldArena, IsActiveBattlefieldArena, JoinBattlefield ... |
| 00ace370 (CommentatorSetMode..) | 16 | `-` | 16 | 0 | CommentatorSetMode, CommentatorToggleMode, CommentatorGetMode, CommentatorSetMapAndInstanceIndex, CommentatorSetPlayerIndex, CommentatorUpdatePlayerInfo ... |
| 00acefe0 (GetTitleText..) | 16 | `-` | 16 | 0 | GetTitleText, GetGreetingText, GetQuestText, GetObjectiveText, GetProgressText, GetRewardText ... |
| 00acd6d8 (SearchLFGGetNumResults..) | 15 | `-` | 15 | 0 | SearchLFGGetNumResults, SearchLFGGetResults, SearchLFGGetEncounterResults, SearchLFGGetPartyResults, SearchLFGSort, GetLFGTypes ... |
| 00ad0c30 (PetHasActionBar..) | 15 | `-` | 15 | 0 | PetHasActionBar, GetPetActionInfo, GetPetActionCooldown, GetPetActionsUsable, GetPetActionSlotUsable, IsPetAttackActive ... |
| 00af29c0 (VoiceEnumerateOutputDevices..) | 15 | `-` | 15 | 0 | VoiceEnumerateOutputDevices, VoiceEnumerateCaptureDevices, VoiceSelectOutputDevice, VoiceSelectCaptureDevice, VoiceGetCurrentOutputDevice, VoiceGetCurrentCaptureDevice ... |
| 00ac8600 (GetRestState..) | 14 | `-` | 14 | 0 | GetRestState, GetXPExhaustion, GetTimeToWellRested, GMRequestPlayerInfo, GetCoinIcon, GetCoinText ... |
| 00accdd0 (GetShapeshiftFormInfo..) | 14 | `-` | 14 | 0 | GetShapeshiftFormInfo, CastShapeshiftForm, GetShapeshiftFormCooldown, CastSpellByName, CastSpellByID, GetNumCompanions ... |
| 00aced08 (GetMerchantNumItems..) | 14 | `-` | 14 | 0 | GetMerchantNumItems, GetMerchantItemInfo, GetMerchantItemCostInfo, GetMerchantItemCostItem, GetBuybackItemInfo, GetBuybackItemLink ... |
| 00ac8578 (RunScript..) | 13 | `-` | 13 | 0 | RunScript, CheckInteractDistance, RandomRoll, OpeningCinematic, InCinematic, IsWindowsClient ... |
| 00ac8970 (SetUIVisibility..) | 13 | `-` | 13 | 0 | SetUIVisibility, IsReferAFriendLinked, CanGrantLevel, GrantLevel, CanSummonFriend, SummonFriend ... |
| 00ace790 (GetNumRaidMembers..) | 16 | `s_ScriptFunctions` | 0 | 13 |  / stubs: GetRealNumRaidMembers, GetRaidRosterInfo, SetRaidRosterSelection, GetRaidRosterSelection ... |
| 00acf0e8 (ConfirmAcceptQuest..) | 13 | `-` | 13 | 0 | ConfirmAcceptQuest, GetQuestBackgroundMaterial, GetSuggestedGroupNum, QuestFlagsPVP, QuestGetAutoAccept, GetDailyQuestsCompleted ... |
| 00acf770 (StablePet..) | 13 | `-` | 13 | 0 | StablePet, UnstablePet, BuyStableSlot, GetNumStablePets, GetNumStableSlots, GetStablePetInfo ... |
| 00acf838 (GetArenaTeam..) | 13 | `-` | 13 | 0 | GetArenaTeam, GetNumArenaTeamMembers, GetArenaTeamRosterInfo, GetArenaTeamGdfInfo, SetArenaTeamRosterSelection, GetArenaTeamRosterSelection ... |
| 00ad0cc0 (PetRename..) | 13 | `-` | 13 | 0 | PetRename, PetCanBeAbandoned, PetCanBeDismissed, PetCanBeRenamed, GetPetTimeRemaining, HasPetUI ... |
| 00ad1a68 (VehicleAimIncrement..) | 13 | `-` | 13 | 0 | VehicleAimIncrement, VehicleAimDecrement, VehicleAimRequestAngle, VehicleAimGetAngle, VehicleAimRequestNormAngle, VehicleAimGetNormAngle ... |
| 00ac1028 (IsObjectType..) | 12 | `-` | 12 | 0 | IsObjectType, GetObjectType, GetDrawLayer, SetDrawLayer, GetBlendMode, SetBlendMode ... |
| 00accff8 (PositionWorldMapArrowFrame..) | 12 | `-` | 12 | 0 | PositionWorldMapArrowFrame, PositionMiniWorldMapArrowFrame, ShowWorldMapArrowFrame, ShowMiniWorldMapArrowFrame, ClickLandmark, GetNumMapDebugObjects ... |
| 00acef78 (GetGossipText..) | 12 | `-` | 12 | 0 | GetGossipText, GetNumGossipOptions, GetNumGossipAvailableQuests, GetNumGossipActiveQuests, GetGossipOptions, GetGossipAvailableQuests ... |
| 00acf080 (GetQuestReward..) | 12 | `-` | 12 | 0 | GetQuestReward, GetRewardMoney, GetRewardXP, GetRewardHonor, GetRewardSpell, GetQuestMoneyToGet ... |
| 00ac42c8 (GetCharacterCreateFacing..) | 11 | `-` | 11 | 0 | GetCharacterCreateFacing, SetCharacterCreateFacing, GetRandomName, CreateCharacter, CustomizeExistingCharacter, PaidChange_GetPreviousRaceIndex ... |
| 00ac8a08 (IsDesaturateSupported..) | 11 | `-` | 11 | 0 | IsDesaturateSupported, GetThreatStatusColor, IsThreatWarningEnabled, ConsoleAddMessage, GetItemUniqueness, EndRefund ... |
| 00acd418 (AccountMsg_LoadHeaders..) | 11 | `-` | 11 | 0 | AccountMsg_LoadHeaders, AccountMsg_GetNumTotalMsgs, AccountMsg_GetNumUnreadMsgs, AccountMsg_GetNumUnreadUrgentMsgs, AccountMsg_GetIndexHighestPriorityUnreadMsg, AccountMsg_GetIndexNextUnreadMsg ... |
| 00acfba8 (EquipmentManagerUnignoreSlotForSave..) | 11 | `-` | 11 | 0 | EquipmentManagerUnignoreSlotForSave, GetEquipmentSetLocations, GetEquipmentSetItemIDs, GetNumEquipmentSets, GetEquipmentSetInfo, GetEquipmentSetInfoByName ... |
| 00acfc70 (GetCategoryList..) | 11 | `-` | 11 | 0 | GetCategoryList, GetStatisticsCategoryList, GetCategoryInfo, GetCategoryNumAchievements, GetComparisonCategoryNumAchievements, GetAchievementInfo ... |
| 00a443a8 (setglobal..) | 10 | `-` | 10 | 0 | setglobal, getglobal, strtrim, strsplit, strjoin, strreplace ... |
| 00ac40e8 (ShowChangedOptionWarnings..) | 10 | `-` | 10 | 0 | ShowChangedOptionWarnings, TokenEntered, GetNumDeclensionSets, DeclineName, GetNumGameAccounts, GetGameAccountInfo ... |
| 00ac8378 (GetZoneText..) | 10 | `-` | 10 | 0 | GetZoneText, GetRealZoneText, GetSubZoneText, GetMinimapZoneText, InitiateTrade, CanInspect ... |
| 00af51c8 (SpellIsTargeting..) | 10 | `-` | 10 | 0 | SpellIsTargeting, SpellCanTargetItem, SpellTargetItem, SpellCanTargetUnit, SpellTargetUnit, SpellCanTargetGlyph ... |
| 00a44400 (securecall..) | 9 | `-` | 9 | 0 | securecall, hooksecurefunc, debugload, debuginfo, debugprint, debugdump ... |
| 00a47cc0 (pcall..) | 9 | `-` | 9 | 0 | pcall, rawequal, rawget, rawset, select, setfenv ... |
| 00ac1098 (IsVisible..) | 15 | `SimpleTextureMethods` | 0 | 9 |  / stubs: GetTexCoord, SetRotation, IsDesaturated, SetNonBlocking ... |
| 00ac3f60 (GetNumAddOns..) | 9 | `-` | 9 | 0 | GetNumAddOns, GetAddOnInfo, LaunchAddOnURL, GetAddOnDependencies, GetAddOnEnableState, EnableAddOn ... |
| 00ac4098 (GetCVar..) | 9 | `-` | 9 | 0 | GetCVar, GetCVarBool, SetCVar, GetCVarDefault, GetCVarMin, GetCVarMax ... |
| 00ac86b8 (GetBindLocation..) | 9 | `-` | 9 | 0 | GetBindLocation, ConfirmTalentWipe, ConfirmBinder, ShowingHelm, ShowingCloak, ShowHelm ... |
| 00acc708 (GetLootMethod..) | 15 | `s_ScriptFunctions` | 0 | 9 |  / stubs: SetLootMethod, SetLootThreshold, SetPartyAssignment, ClearPartyAssignment ... |
| 00ad09b8 (GetNumSkillLines..) | 9 | `-` | 9 | 0 | GetNumSkillLines, GetSkillLineInfo, AbandonSkill, CollapseSkillHeader, ExpandSkillHeader, AddSkillUp ... |
| 00b2d418 (Enable..) | 34 | `SimpleButtonMethods` | 0 | 9 |  / stubs: GetNormalFontObject, GetDisabledFontObject, GetHighlightFontObject, SetFontString ... |
| 00ac1118 (IsObjectType..) | 8 | `-` | 8 | 0 | IsObjectType, GetObjectType, GetDrawLayer, SetDrawLayer, SetVertexColor, GetAlpha ... |
| 00acd200 (GetNumBattlefieldScores..) | 8 | `-` | 8 | 0 | GetNumBattlefieldScores, GetBattlefieldScore, GetBattlefieldWinner, SetBattlefieldScoreFaction, LeaveBattlefield, GetNumBattlefieldStats ... |
| 00ad02e0 (CalendarEventGetTextures..) | 8 | `-` | 8 | 0 | CalendarEventGetTextures, CalendarEventHasPendingInvite, CalendarEventHaveSettingsChanged, CalendarEventCanEdit, CalendarEventGetCalendarType, CalendarCanSendInvite ... |
| 00a47c80 (assert..) | 7 | `-` | 7 | 0 | assert, collectgarbage, error, gcinfo, getfenv, getmetatable ... |
| 00ac1480 (IsProtected..) | 25 | `ScriptRegionMethods` | 0 | 7 |  / stubs: IsProtected, CanChangeProtectedState, GetRect, CreateAnimationGroup ... |
| 00accf18 (GetMapContinents..) | 7 | `-` | 7 | 0 | GetMapContinents, GetMapZones, SetMapZoom, ZoomOut, SetDungeonMapLevel, GetNumDungeonMapLevels ... |
| 00acedb0 (CloseTrade..) | 7 | `s_ScriptFunctions` | 0 | 7 |  / stubs: CloseTrade, ClickTradeButton, ClickTargetTradeButton, GetTradeTargetItemInfo ... |
| 00acf290 (TaxiNodeGetType..) | 7 | `-` | 7 | 0 | TaxiNodeGetType, TaxiNodeSetCurrent, TaxiGetSrcX, TaxiGetSrcY, TaxiGetDestX, TaxiGetDestY ... |
| 00acf7f8 (GetNumPetitionItems..) | 7 | `-` | 7 | 0 | GetNumPetitionItems, GetPetitionItemInfo, BuyPetition, ClickPetitionButton, TurnInPetition, TurnInArenaPetition ... |
| 00ad00c0 (CalendarGetEventInfo..) | 7 | `-` | 7 | 0 | CalendarGetEventInfo, CalendarGetHolidayInfo, CalendarGetRaidInfo, CalendarGetNumPendingInvites, CalendarEventGetNumInvites, CalendarEventGetInvite ... |
| 00ad0570 (GetSocketItemInfo..) | 7 | `-` | 7 | 0 | GetSocketItemInfo, GetNumSockets, GetExistingSocketInfo, GetExistingSocketLink, GetNewSocketInfo, GetNewSocketLink ... |
| 00ad0a68 (GetPetitionInfo..) | 7 | `-` | 7 | 0 | GetPetitionInfo, GetNumPetitionNames, GetPetitionNameInfo, CanSignPetition, SignPetition, OfferPetition ... |
| 00ad0ae8 (GetNumFactions..) | 7 | `-` | 7 | 0 | GetNumFactions, GetFactionInfo, GetFactionInfoByID, GetWatchedFactionInfo, SetWatchedFactionIndex, FactionToggleAtWar ... |
| 00adb8e8 (CombatLogAddFilter..) | 7 | `-` | 7 | 0 | CombatLogAddFilter, CombatLogSetRetentionTime, CombatLogGetRetentionTime, CombatLogGetNumEntries, CombatLogSetCurrentEntry, CombatLogGetCurrentEntry ... |
| 00ac19f0 (SetOrigin..) | 6 | `-` | 6 | 0 | SetOrigin, GetOrigin, SetDegrees, GetDegrees, SetRadians, GetRadians |
| 00ac3e78 (GetMovieResolution..) | 6 | `-` | 6 | 0 | GetMovieResolution, GetScreenWidth, GetScreenHeight, LaunchURL, ShowTOSNotice, TOSAccepted |
| 00ac3fe0 (GetBillingTimeRemaining..) | 6 | `-` | 6 | 0 | GetBillingTimeRemaining, GetBillingPlan, GetBillingTimeRested, SurveyNotificationDone, PINEntered, PlayGlueAmbience |
| 00ac4060 (IsScanDLLFinished..) | 6 | `-` | 6 | 0 | IsScanDLLFinished, IsWindowsClient, IsMacClient, IsLinuxClient, SetRealmSplitState, RequestRealmSplitInfo |
| 00ac8338 (AssistUnit..) | 6 | `-` | 6 | 0 | AssistUnit, FocusUnit, FollowUnit, InteractUnit, ClearTarget, ClearFocus |
| 00acc6d0 (GetNumPartyMembers..) | 6 | `-` | 6 | 0 | GetNumPartyMembers, GetRealNumPartyMembers, GetPartyMember, GetPartyLeaderIndex, IsPartyLeader, IsRealPartyLeader |
| 00accce0 (GetNumSpellTabs..) | 25 | `s_ScriptFunctions` | 6 | 0 | GetSpellCount, ToggleSpellAutocast, EnableSpellAutocast, DisableSpellAutocast, PickupSpell, SpellHasRange |
| 00aceec0 (ItemTextGetItem..) | 6 | `-` | 6 | 0 | ItemTextGetItem, ItemTextGetCreator, ItemTextGetMaterial, ItemTextGetPage, ItemTextGetText, ItemTextHasNextPage |
| 00acf258 (SetTaxiMap..) | 6 | `-` | 6 | 0 | SetTaxiMap, NumTaxiNodes, TaxiNodeName, TaxiNodePosition, TaxiNodeCost, TakeTaxiNode |
| 00acf640 (GetAuctionHouseDepositRate..) | 6 | `-` | 6 | 0 | GetAuctionHouseDepositRate, CalculateAuctionDeposit, ClickAuctionSellItemButton, GetAuctionSellItemInfo, StartAuction, QueryAuctionItems |
| 00acf9e0 (GetActionInfo..) | 28 | `s_ScriptFunctions` | 0 | 6 |  / stubs: GetActionAutocast, PickupAction, PlaceAction, ChangeActionBarPage ... |
| 00acfc34 (GetCurrencyListSize..) | 6 | `-` | 6 | 0 | GetCurrencyListSize, GetCurrencyListInfo, ExpandCurrencyList, SetCurrencyUnused, SetCurrencyBackpack, GetBackpackCurrencyInfo |
| 00acff54 (GetNumGlyphSockets..) | 6 | `-` | 6 | 0 | GetNumGlyphSockets, GetGlyphSocketInfo, GlyphMatchesSocket, PlaceGlyphInSocket, RemoveGlyphFromSocket, GetGlyphLink |
| 00ad1208 (QuestPOIGetIconInfo..) | 6 | `-` | 6 | 0 | QuestPOIGetIconInfo, QuestPOIGetQuestIDByIndex, QuestPOIGetQuestIDByVisibleIndex, GetQuestLogCompletionText, SetPOIIconOverlapDistance, SetPOIIconOverlapPushDistance |
| 00af5848 (GetText..) | 7 | `s_ScriptFunctions` | 0 | 6 |  / stubs: GetText, GetNumFrames, EnumerateFrames, CreateFont ... |
| 00ac1818 (GetObjectType..) | 5 | `-` | 5 | 0 | GetObjectType, IsObjectType, GetName, SetFontObject, GetFontObject |
| 00ac1a48 (SetParent..) | 5 | `-` | 5 | 0 | SetParent, SetOffset, GetOffset, SetOrder, GetOrder |
| 00ac1a74 (SetCurve..) | 5 | `-` | 5 | 0 | SetCurve, GetCurve, GetControlPoints, CreateControlPoint, GetMaxOrder |
| 00ac4160 (IsConsoleActive..) | 5 | `-` | 5 | 0 | IsConsoleActive, RunScript, ReadyForAccountDataTimes, IsTrialAccount, IsSystemSupported |
| 00ac8678 (SetPortraitToTexture..) | 5 | `-` | 5 | 0 | SetPortraitToTexture, GetLocale, GetGMTicketCategories, DropItemOnUnit, RestartGx |
| 00acd680 (GetLFGInfoLocal..) | 5 | `-` | 5 | 0 | GetLFGInfoLocal, GetLFGInfoServer, GetLFGQueuedList, SetLFGComment, LFGTeleport |
| 00aceb7c (BankButtonIDToInvSlotID..) | 5 | `-` | 5 | 0 | BankButtonIDToInvSlotID, GetNumBankSlots, GetBankSlotCost, PurchaseSlot, CloseBankFrame |
| 00acebb8 (SetMaskTexture..) | 19 | `CGMinimapFrameMethods` | 4 | 1 | GetNumTrackingTypes, GetTrackingInfo, SetTracking, GetTrackingTexture / stubs: SetMaskTexture |
| 00acfb78 (SaveEquipmentSet..) | 5 | `-` | 5 | 0 | SaveEquipmentSet, DeleteEquipmentSet, RenameEquipmentSet, EquipmentManagerIgnoreSlotForSave, EquipmentManagerIsSlotIgnoredForSave |
| 00ad0970 (GetGuildInfoText..) | 5 | `-` | 5 | 0 | GetGuildInfoText, SetGuildInfoText, QueryGuildEventLog, GetNumGuildEvents, GetGuildEventInfo |
| 00ad1350 (GetInspectHonorData..) | 5 | `-` | 5 | 0 | GetInspectHonorData, GetInspectArenaTeamData, ClearInspectPlayer, GetWeaponEnchantInfo, HasWandEquipped |
| 00b2cb10 (SetModel..) | 24 | `SimpleModelMethods` | 0 | 5 |  / stubs: SetCamera, AdvanceTime, SetFogColor, SetFogNear ... |
| 00b2ce20 (GetThumbTexture..) | 13 | `SimpleSliderMethods` | 0 | 5 |  / stubs: GetThumbTexture, SetThumbTexture, GetOrientation, SetOrientation ... |
| 00a44478 (difftime..) | 4 | `-` | 4 | 0 | difftime, debugstack, debuglocals, scrub |
| 00a47d28 (create..) | 4 | `-` | 4 | 0 | create, resume, running, status |
| 00ac1a24 (SetOrigin..) | 4 | `-` | 4 | 0 | SetOrigin, GetOrigin, SetScale, GetScale |
| 00ac4018 (GetCreditsText..) | 4 | `-` | 4 | 0 | GetCreditsText, GetAccountExpansionLevel, GetClientExpansionLevel, MatrixEntered |
| 00acc868 (CanResetTutorials..) | 4 | `-` | 4 | 0 | CanResetTutorials, FlagTutorial, IsTutorialFlagged, TriggerTutorial |
| 00acd320 (GetScreenResolutions..) | 15 | `CGVideoOptions::s_ScriptFunctions` | 0 | 4 |  / stubs: SetupFullscreenScale, SetMultisampleFormat, GetVideoCaps, SetTerrainMip |
| 00aced88 (InRepairMode..) | 4 | `-` | 4 | 0 | InRepairMode, GetRepairAllCost, RepairAllItems, GetNumBuybackItems |
| 00ad0b28 (SetFactionInactive..) | 4 | `-` | 4 | 0 | SetFactionInactive, SetFactionActive, IsFactionInactive, ExpandFactionHeader |
| 00ad1020 (GetNumQuestLogEntries..) | 4 | `-` | 4 | 0 | GetNumQuestLogEntries, GetQuestLogTitle, SelectQuestLogEntry, GetQuestLogSelection |
| 00b2d150 (SetFontObject..) | 23 | `SimpleHTMLMethods` | 0 | 4 |  / stubs: SetFontObject, SetText, SetHyperlinkFormat, GetHyperlinkFormat |
| 00b2d970 (Sound_ChatSystem_GetNumInputDrivers..) | 4 | `-` | 4 | 0 | Sound_ChatSystem_GetNumInputDrivers, Sound_ChatSystem_GetInputDriverNameByIndex, Sound_ChatSystem_GetNumOutputDrivers, Sound_ChatSystem_GetOutputDriverNameByIndex |
| 00ac3e00 (IsShiftKeyDown..) | 10 | `s_ScriptFunctions` | 0 | 2 |  / stubs: SetUsesToken, SetSavedAccountList |
| 00ac41a8 (GetNumRealms..) | 7 | `s_ScriptFunctions` | 0 | 2 |  / stubs: SetPreferredInfo, GetSelectedCategory |
| 00ac4390 (GetNumCharacters..) | 6 | `s_ScriptFunctions` | 0 | 2 |  / stubs: RenameCharacter, DeclineCharacter |
| 00b2cd50 (GetColorWheelTexture..) | 12 | `SimpleColorSelectMethods` | 2 | 0 | SetColorWheelThumbTexture, SetColorValueThumbTexture |
| 00b2cdb8 (GetOrientation..) | 12 | `SimpleStatusBarMethods` | 0 | 2 |  / stubs: SetStatusBarTexture, SetStatusBarColor |
| 00b2ce90 (SetScrollChild..) | 9 | `SimpleScrollFrameMethods` | 0 | 2 |  / stubs: SetScrollChild, UpdateScrollChildRect |
| 00b2d3e4 (SetChecked..) | 6 | `SimpleCheckboxMethods` | 0 | 2 |  / stubs: SetCheckedTexture, SetDisabledCheckedTexture |
| 00ac4240 (GetNameForRace..) | 13 | `s_ScriptFunctions` | 0 | 1 |  / stubs: GetClassesForRace |
| 00ac465c (ResetLights..) | 4 | `SimpleModelFFXMethods` | 0 | 1 |  / stubs: ResetLights |
| 00acf180 (SetFillTexture..) | 14 | `CGQuestPOIFrameMethods` | 0 | 1 |  / stubs: SetFillTexture |
| 00acf514 (SetUnit..) | 4 | `CGCharacterModelBaseMethods` | 0 | 1 |  / stubs: SetUnit |
| 00ad13a4 (SetCooldown..) | 5 | `CGCooldownMethods` | 0 | 1 |  / stubs: SetCooldown |

## Packet handler coverage (SetMessageHandler)

The reference registers handlers for 579 opcodes; whoa registers 35 of them. An opcode with no whoa handler is a server message the client silently drops.

Reference-only, by opcode (handler address): SMSG_CHECK_FOR_BOTS 006b9670, SMSG_FORCEACTIONSHOW 006e2e90, SMSG_PETGODMODE 006e2e90, SMSG_REFER_A_FRIEND_EXPIRED 00526530, SMSG_GOD_MODE 006e2e90, SMSG_DESTRUCTIBLE_BUILDING_DAMAGE 00753730, SMSG_QUERY_PET_NAME_RESPONSE 006352c0, SMSG_QUERY_GUILD_INFO_RESPONSE 006359e0, SMSG_ITEM_QUERY_SINGLE_RESPONSE 006354d0, SMSG_ITEM_QUERY_MULTIPLE_RESPONSE 006351f0, SMSG_QUERY_PAGE_TEXT_RESPONSE 006352a0, SMSG_QUERY_QUEST_INFO_RESPONSE 00635230, SMSG_QUERY_GAME_OBJECT_RESPONSE 006351b0, SMSG_WHO 006b8720, SMSG_WHO_IS 006b3280, SMSG_CONTACT_LIST 006b8700, SMSG_FRIEND_STATUS 006b86b0, SMSG_GROUP_INVITE 006df1a0, SMSG_GROUP_CANCEL 006cbd10, SMSG_GROUP_DECLINE 006cbd40, SMSG_GROUP_SET_LEADER 006cbd70, SMSG_GROUP_LIST 006d8870, SMSG_PARTY_MEMBER_STATS 006cf9b0, SMSG_PARTY_COMMAND_RESULT 006cbec0, SMSG_GUILD_INVITE 006cc3b0, SMSG_GUILD_DECLINE 006cc410, SMSG_GUILD_INFO 006cc440, SMSG_GUILD_ROSTER 005cc5d0, SMSG_GUILD_EVENT 006d92d0, SMSG_GUILD_COMMAND_RESULT 006cc590, SMSG_CHAT 0050eba0, SMSG_CHANNEL_NOTIFY 0050e120, SMSG_CHANNEL_LIST 00505dc0, SMSG_READ_ITEM_RESULT_OK 006dbdf0, SMSG_READ_ITEM_RESULT_FAILED 006dbdf0, SMSG_ITEM_COOLDOWN 00807060, SMSG_GAME_OBJECT_CUSTOM_ANIM 0070be90, MSG_MOVE_START_FORWARD 00741b60, MSG_MOVE_START_BACKWARD 00741b60, MSG_MOVE_STOP 00741b60, MSG_MOVE_START_STRAFE_LEFT 00741b60, MSG_MOVE_START_STRAFE_RIGHT 00741b60, MSG_MOVE_STOP_STRAFE 00741b60, MSG_MOVE_JUMP 00741b60, MSG_MOVE_START_TURN_LEFT 00741b60, MSG_MOVE_START_TURN_RIGHT 00741b60, MSG_MOVE_STOP_TURN 00741b60, MSG_MOVE_START_PITCH_UP 00741b60, MSG_MOVE_START_PITCH_DOWN 00741b60, MSG_MOVE_STOP_PITCH 00741b60, MSG_MOVE_SET_RUN_MODE 00741b60, MSG_MOVE_SET_WALK_MODE 00741b60, MSG_MOVE_TELEPORT 00741b60, MSG_MOVE_TELEPORT_ACK 00732450, MSG_MOVE_FALL_LAND 00741b60, MSG_MOVE_START_SWIM 00741b60, MSG_MOVE_STOP_SWIM 00741b60, MSG_MOVE_SET_RUN_SPEED 00741b00, MSG_MOVE_SET_RUN_BACK_SPEED 00741b00, MSG_MOVE_SET_WALK_SPEED 00741b00, MSG_MOVE_SET_SWIM_SPEED 00741b00, MSG_MOVE_SET_SWIM_BACK_SPEED 00741b00, MSG_MOVE_SET_TURN_RATE 00741b00, MSG_MOVE_TOGGLE_COLLISION_CHEAT 00741b60, MSG_MOVE_SET_FACING 00741b60, MSG_MOVE_SET_PITCH 00741b60, SMSG_ON_MONSTER_MOVE 0073f590, SMSG_MOVE_SET_WATER_WALK 00732450, SMSG_MOVE_SET_LAND_WALK 00732450, SMSG_FORCE_RUN_SPEED_CHANGE 00732450, SMSG_FORCE_RUN_BACK_SPEED_CHANGE 00732450, SMSG_FORCE_SWIM_SPEED_CHANGE 00732450, SMSG_FORCE_MOVE_ROOT 00732450, SMSG_FORCE_MOVE_UNROOT 00732450, 0xec 00741b60, 0xed 00741b60, 0xee 00741b60, SMSG_MOVE_KNOCK_BACK 00732450, 0xf1 00741b60, SMSG_MOVE_SET_FEATHER_FALL 00732450, SMSG_MOVE_SET_NORMAL_FALL 00732450, SMSG_MOVE_SET_HOVERING 00732450, SMSG_MOVE_UNSET_HOVERING 00732450, 0xf7 00741b60, SMSG_TRIGGER_CINEMATIC 006e2e90, SMSG_TUTORIAL_FLAGS 00530920, SMSG_TEXT_EMOTE 00504070, SMSG_OPEN_CONTAINER 006e2e90, SMSG_INSPECT_RESULTS_UPDATE 006ce070, SMSG_TRADE_STATUS 007044a0, SMSG_TRADE_STATUS_EXTENDED 00704680, SMSG_INITIALIZE_FACTIONS 005d2e30, SMSG_SET_FACTION_VISIBLE 005d2050, SMSG_SET_FACTION_STANDING 005d20a0, SMSG_SET_PROFICIENCY 006cdeb0, SMSG_SUPERCEDED_SPELLS 006e7e00, SMSG_CAST_FAILED 00809af0, SMSG_SPELL_COOLDOWN 00806dd0, SMSG_COOLDOWN_EVENT 00804010, SMSG_EQUIPMENT_SET_ID 005af6c0, SMSG_PET_CAST_FAILED 00806c30, SMSG_AI_REACTION 00716b10, SMSG_ATTACK_START 00756800, SMSG_ATTACK_STOP 00756800, SMSG_ATTACKSWING_NOTINRANGE 00756800, SMSG_ATTACKSWING_BADFACING 00756800, SMSG_INSTANCE_LOCK_WARNING_QUERY 006e2e90, SMSG_ATTACKSWING_DEADTARGET 00756800, SMSG_ATTACKSWING_CANT_ATTACK 00756800, SMSG_ATTACKER_STATE_UPDATE 00756800, SMSG_RESUME_CAST_BAR 008020c0, SMSG_SPELL_BREAK_LOG 006ce3b0, SMSG_SPELL_HEAL_LOG 006d3dd0, SMSG_SPELL_ENERGIZE_LOG 006d3ef0, SMSG_BREAK_TARGET 00526530, SMSG_BIND_POINT_UPDATE 006e2e90, SMSG_BINDZONEREPLY 006e2e90, SMSG_PLAYER_BOUND 006e2e90, SMSG_CONTROL_UPDATE 0072d0b0, SMSG_RESURRECT_REQUEST 006dbd00, SMSG_LOOT_RESPONSE 006d84f0, SMSG_LOOT_RELEASE 006d84f0, SMSG_LOOT_REMOVED 006d84f0, SMSG_LOOT_MONEY_NOTIFY 006d84f0, SMSG_LOOT_ITEM_NOTIFY 006d84f0, SMSG_LOOT_CLEAR_MONEY 006d84f0, SMSG_ITEM_PUSH_RESULT 006e2e90, SMSG_DUEL_REQUESTED 005cfcd0, SMSG_DUEL_OUT_OF_BOUNDS 005cf910, SMSG_DUEL_IN_BOUNDS 005cf930, SMSG_DUEL_COMPLETE 005cfa90, SMSG_DUEL_WINNER 005cfb20, SMSG_MOUNT_RESULT 006e2e90, SMSG_DISMOUNT_RESULT 006e2e90, SMSG_REMOVED_FROM_PVP_QUEUE 00526530, SMSG_MOUNT_SPECIAL_ANIM 0071cb30, SMSG_PET_TAME_FAILURE 00802090, SMSG_PET_NAME_INVALID 006e2e90, SMSG_PET_SPELLS_MESSAGE 005d6b90, SMSG_PET_MODE 005d3630, SMSG_GOSSIP_MESSAGE 0058b1b0, SMSG_GOSSIP_COMPLETE 0058a840, SMSG_QUERY_NPC_TEXT_RESPONSE 00635210, SMSG_QUEST_GIVER_QUEST_LIST_MESSAGE 006d7f10, SMSG_QUEST_GIVER_QUEST_DETAILS 006d7f10, SMSG_QUEST_GIVER_REQUEST_ITEMS 006d7f10, SMSG_QUEST_GIVER_OFFER_REWARD_MESSAGE 006d7f10, SMSG_QUEST_GIVER_INVALID_QUEST 006d7f10, SMSG_QUEST_GIVER_QUEST_COMPLETE 006d7f10, SMSG_QUEST_GIVER_QUEST_FAILED 006d7f10, SMSG_QUEST_LOG_FULL 006d7f10, SMSG_QUEST_UPDATE_FAILED 006d8030, SMSG_QUEST_UPDATE_FAILED_TIMER 006d8030, SMSG_QUEST_UPDATE_COMPLETE 006d8030, SMSG_QUEST_UPDATE_ADD_KILL 006d8030, SMSG_QUEST_CONFIRM_ACCEPT 006cbc50, SMSG_VENDOR_INVENTORY 006defa0, SMSG_SELL_ITEM 006defa0, SMSG_BUY_SUCCEEDED 006defa0, SMSG_BUY_FAILED 006defa0, SMSG_SHOW_TAXI_NODES 006e2e90, SMSG_TAXI_NODE_STATUS 006e2e90, SMSG_ACTIVATE_TAXI_REPLY 006e2e90, SMSG_NEW_TAXI_PATH 006e2e90, SMSG_TRAINER_LIST 006d8410, SMSG_TRAINER_BUY_FAILED 006d8410, SMSG_PLAYERBINDERROR 006e2e90, SMSG_SHOW_BANK 006e2e90, SMSG_BUY_BANK_SLOT_RESULT 006e2e90, SMSG_PETITION_SHOW_LIST 006deef0, SMSG_PETITION_SHOW_SIGNATURES 006deef0, SMSG_PETITION_SIGN_RESULTS 006deef0, 0x1c2 006deef0, SMSG_TURN_IN_PETITION_RESULT 006deef0, SMSG_PETITION_QUERY_RESPONSE 00635390, SMSG_FISH_NOT_HOOKED 006e2e90, SMSG_FISH_ESCAPED 006e2e90, SMSG_QUERY_TIME_RESPONSE 005e08a0, SMSG_LOG_XP_GAIN 0050c720, SMSG_LEVEL_UP_INFO 006e2e90, 0x1d5 006e2e90, SMSG_ENCHANTMENT_LOG 00751050, SMSG_START_MIRROR_TIMER 00519a50, SMSG_PAUSE_MIRROR_TIMER 00519a50, SMSG_STOP_MIRROR_TIMER 00519a50, SMSG_CLEAR_COOLDOWN 00804010, SMSG_PAGE_TEXT 0070be30, SMSG_COOLDOWN_CHEAT 00804110, SMSG_SPELL_DELAYED 00801b80, SMSG_QUEST_POI_QUERY_RESPONSE 006cbcb0, SMSG_INVALID_PROMOTION_CODE 00526530, SMSG_ITEM_TIME_UPDATE 006e6330, SMSG_ITEM_ENCHANT_TIME_UPDATE 006e6330, 0x1f1 006d9230, 0x1f2 006cc560, SMSG_PLAY_SPELL_VISUAL 00800610, SMSG_PARTY_KILL_LOG 00750c90, SMSG_PLAY_SPELL_IMPACT 008006c0, SMSG_EXPLORATION_EXPERIENCE 006e2e90, 0x1fb 006e2e90, SMSG_ENVIRONMENTAL_DAMAGE_LOG 00756800, SMSG_RWHOIS 006b32c0, SMSG_LFG_PLAYER_REWARD 0055bdc0, SMSG_LFG_TELEPORT_DENIED 0055bdc0, SMSG_GM_TICKET_CREATE 005ad240, SMSG_GM_TICKET_UPDATE_TEXT 005ad240, SMSG_ACCOUNT_DATA_TIMES 006b94c0, SMSG_UPDATE_ACCOUNT_DATA 006b9730, SMSG_CLEAR_FAR_SIGHT_IMMEDIATE 006e2e90, SMSG_CHANGE_PLAYER_DIFFICULTY_RESULT 0052c460, SMSG_GM_TICKET_GET_TICKET 005ad240, SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT 005eddd0, SMSG_GAMEOBJECT_DESPAWN_ANIM 0070bef0, 0x216 00526530, SMSG_GM_TICKET_DELETE_TICKET 005ad240, SMSG_CHAT_WRONG_FACTION 006e2e90, SMSG_GM_TICKET_GET_SYSTEM_STATUS 006e2e90, SMSG_QUEST_FORCE_REMOVED 006d8370, SMSG_SPIRIT_HEALER_CONFIRM 006e2e90, SMSG_GOSSIP_POI 0058a870, CMSG_GM_REQUEST_PLAYER_INFO 00526530, SMSG_GM_PLAYER_INFO 00526530, SMSG_MAIL_COMMAND_RESULT 005717b0, SMSG_MAIL_LIST_RESULT 00571c50, SMSG_BATTLEFIELD_LIST 0054e390, SMSG_QUERY_ITEM_TEXT_RESPONSE 00635a40, SMSG_SPELL_MISS_LOG 006d3200, SMSG_SPELL_EXECUTE_LOG 006d3730, SMSG_SPELL_PERIODIC_AURA_LOG 00753710, SMSG_SPELL_DAMAGE_SHIELD 006d3750, SMSG_SPELL_NON_MELEE_DAMAGE_LOG 006d3c10, SMSG_ZONE_UNDER_ATTACK 0050c3c0, 0x255 0059ff40, SMSG_AUCTION_COMMAND_RESULT 0059ffb0, SMSG_AUCTION_LIST_RESULT 0059e160, SMSG_AUCTION_LIST_OWNER_ITEMS_RESULT 0059e480, SMSG_AUCTION_BIDDER_NOTIFICATION 005a0480, SMSG_AUCTION_OWNER_NOTIFICATION 005a0790, SMSG_PROC_RESIST 00750d40, SMSG_COMBAT_EVENT_FAILED 00756800, SMSG_DISPEL_FAILED 00750ea0, SMSG_SPELL_OR_DAMAGE_IMMUNE 006ce3d0, SMSG_AUCTION_LIST_BIDDER_ITEMS_RESULT 0059ecd0, SMSG_SET_FLAT_SPELL_MODIFIER 007fdc60, SMSG_SET_PCT_SPELL_MODIFIER 007fdc60, SMSG_CORPSE_RECLAIM_DELAY 00526530, 0x26f 005a11a0, SMSG_STABLE_RESULT 005a17f0, 0x276 006e2e90, SMSG_PLAY_MUSIC 00526530, SMSG_PLAY_OBJECT_SOUND 00526530, SMSG_SPELL_DISPELL_LOG 006ce3b0, SMSG_DAMAGE_CALC_LOG 006d3f10, 0x284 0056daf0, SMSG_RECEIVED_MAIL 00571a10, SMSG_RAID_GROUP_ONLY 006e2e90, SMSG_PVP_CREDIT 00526530, SMSG_AUCTION_REMOVED_NOTIFICATION 005a0ac0, SMSG_CHAT_SERVER_MESSAGE 0050c980, SMSG_LFG_OFFER_CONTINUE 0055bdc0, SMSG_TEST_DROP_RATE_RESULT 006d84f0, SMSG_SHOW_MAILBOX 0056dbc0, SMSG_RESET_RANGED_COMBAT_TIMER 008071c0, SMSG_CHAT_NOT_IN_PARTY 006e2e90, SMSG_CANCEL_AUTO_REPEAT 0072d130, SMSG_STAND_STATE_UPDATE 0073f540, SMSG_LOOT_ALL_PASSED 006d84f0, SMSG_LOOT_ROLL_WON 006d84f0, SMSG_LOOT_START_ROLL 006d84f0, SMSG_LOOT_ROLL 006d84f0, SMSG_LOOT_MASTER_LIST 006d84f0, SMSG_SET_FORCED_REACTIONS 005d15d0, SMSG_GAME_OBJECT_RESET_STATE 007fd900, SMSG_CHAT_PLAYER_NOTFOUND 006e2e90, 0x2aa 006e2e90, SMSG_SUMMON_REQUEST 006d8680, SMSG_MONSTER_MOVE_TRANSPORT 0073f590, 0x2b0 00741b60, 0x2b1 00741b60, SMSG_DUEL_COUNTDOWN 005cfa50, SMSG_AREA_TRIGGER_MESSAGE 00526530, SMSG_LFG_ROLE_CHOSEN 0055bdc0, SMSG_PLAYER_SKINNED 006cdf00, 0x2c1 006deef0, SMSG_INIT_WORLD_STATES 00526530, SMSG_UPDATE_WORLD_STATE 00526530, SMSG_ITEM_NAME_QUERY_RESPONSE 006351d0, SMSG_PET_ACTION_FEEDBACK 005d4da0, SMSG_INSTANCE_SAVE_CREATED 0052bb10, SMSG_RAID_INSTANCE_INFO 00501030, SMSG_PLAY_SOUND 00526530, SMSG_BATTLEFIELD_STATUS 0054ae40, 0x2d6 005e7a50, SMSG_FORCE_WALK_SPEED_CHANGE 00732450, SMSG_FORCE_SWIM_BACK_SPEED_CHANGE 00732450, SMSG_FORCE_TURN_RATE_CHANGE 00732450, 0x2e0 0054d280, SMSG_AREA_SPIRIT_HEALER_TIME 00526530, SMSG_WARDEN_DATA 007da850, SMSG_BATTLEFIELD_STATUS_QUEUED 0054b1c0, 0x2e9 0054b3f0, SMSG_BINDER_CONFIRM 006e2e90, SMSG_BATTLEGROUND_PLAYER_JOINED 0054b510, SMSG_BATTLEGROUND_PLAYER_LEFT 0054b510, SMSG_PARTY_MEMBER_STATS_FULL 006cf9b0, SMSG_PLAY_TIME_WARNING 006cd270, SMSG_MINIGAME_SETUP 005c5690, SMSG_MINIGAME_STATE 005c54d0, SMSG_RAID_INSTANCE_MESSAGE 0050ca80, SMSG_COMPRESSED_MOVES 00716ad0, SMSG_CHAT_RESTRICTED 006e2e90, SMSG_MOVE_SPLINE_SET_RUN_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_RUN_BACK_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_SWIM_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_WALK_BACK_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_SWIM_BACK_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_TURN_RATE 00741bc0, SMSG_MOVE_SPLINE_UNROOT 00741c30, SMSG_MOVE_SPLINE_SET_FEATHER_FALL 00741c30, SMSG_MOVE_SPLINE_SET_NORMAL_FALL 00741c30, SMSG_MOVE_SPLINE_SET_HOVER 00741c30, SMSG_MOVE_SPLINE_UNSET_HOVER 00741c30, SMSG_MOVE_SPLINE_SET_WATER_WALK 00741c30, SMSG_MOVE_SPLINE_SET_LAND_WALK 00741c30, SMSG_MOVE_SPLINE_START_SWIM 00741c30, SMSG_MOVE_SPLINE_STOP_SWIM 00741c30, SMSG_MOVE_SPLINE_SET_RUN_MODE 00741c30, SMSG_MOVE_SPLINE_SET_WALK_MODE 00741c30, SMSG_SET_FACTION_AT_WAR 005d0850, 0x319 0071cab0, SMSG_MOVE_SPLINE_ROOT 00741c30, SMSG_INVALIDATE_PLAYER 00635400, SMSG_INSTANCE_RESET 0050ccd0, SMSG_INSTANCE_RESET_FAILED 0050cda0, SMSG_UPDATE_LAST_INSTANCE 004fe100, 0x321 005743b0, 0x322 00574d50, SMSG_PET_ACTION_SOUND 00716b90, SMSG_PET_DISMISS_SOUND 00716c00, SMSG_GM_TICKET_STATUS_UPDATE 005ad240, 0x329 00526530, SMSG_UPDATE_INSTANCE_OWNERSHIP 004fb990, SMSG_SPELL_INSTAKILL_LOG 006ce260, SMSG_SPELL_UPDATE_CHAIN_TARGETS 00800470, SMSG_EXPECTED_SPAM_RECORDS 00501c70, SMSG_SPELL_STEAL_LOG 006ce3b0, SMSG_DEFENSE_MESSAGE 0050c850, SMSG_INSTANCE_DIFFICULTY 00526530, SMSG_MOTD 00551660, SMSG_MOVE_ENABLE_TRANSITION_BETWEEN_SWIM_AND_FLY 00732450, SMSG_MOVE_DISABLE_TRANSITION_BETWEEN_SWIM_AND_FLY 00732450, 0x341 00741b60, 0x342 00741b60, SMSG_MOVE_SET_CAN_FLY 00732450, SMSG_MOVE_UNSET_CAN_FLY 00732450, SMSG_ARENA_TEAM_COMMAND_RESULT 006ccb20, 0x34a 00741b60, SMSG_ARENA_TEAM_QUERY_RESPONSE 00635480, SMSG_ARENA_TEAM_ROSTER 005a3e10, SMSG_ARENA_TEAM_INVITE 006cc910, SMSG_ARENA_TEAM_EVENT 006cc980, 0x359 00741b60, 0x35a 00741b60, SMSG_ARENA_TEAM_STATS 005a2d50, SMSG_LFG_LFR_LIST 0055b770, SMSG_LFG_PROPOSAL_UPDATE 0055bdc0, SMSG_LFG_ROLE_CHECK_UPDATE 0055bdc0, SMSG_LFG_JOIN_RESULT 0055bdc0, SMSG_LFG_QUEUE_STATUS 0055bdc0, SMSG_LFG_UPDATE_PLAYER 0055bdc0, SMSG_LFG_UPDATE_PARTY 0055bdc0, SMSG_LFG_UPDATE_SEARCH 0055bdc0, SMSG_LFG_BOOT_PROPOSAL_UPDATE 0055bdc0, SMSG_LFG_PLAYER_INFO 0055bdc0, SMSG_LFG_PARTY_INFO 0055bdc0, SMSG_TITLE_EARNED 0050c520, SMSG_ARENA_ERROR 006cce90, 0x377 005e7b00, SMSG_DEATH_RELEASE_LOC 006e2e90, SMSG_FORCED_DEATH_UPDATE 006e2e90, 0x37e 00741b00, 0x380 00741b00, SMSG_FORCE_FLIGHT_SPEED_CHANGE 00732450, SMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE 00732450, SMSG_MOVE_SPLINE_SET_FLIGHT_SPEED 00741bc0, SMSG_MOVE_SPLINE_SET_FLIGHT_BACK_SPEED 00741bc0, SMSG_FLIGHT_SPLINE_SYNC 00716940, SMSG_OFFER_PETITION_ERROR 005cea00, SMSG_TIME_SYNC_REQUEST 006dc010, SMSG_REAL_GROUP_UPDATE 006cc300, SMSG_LFG_DISABLED 0055bdc0, SMSG_CHEAT_DUMP_ITEMS_DEBUG_ONLY_RESPONSE 006354d0, SMSG_UPDATE_COMBO_POINTS 00526530, SMSG_VOICE_SESSION_ROSTER_UPDATE 006ccf10, SMSG_VOICE_SESSION_LEAVE 006cd1b0, SMSG_VOICE_SET_TALKER_MUTED 006cd210, 0x3a7 00741b60, SMSG_IGNORE_REQUIREMENTS_CHEAT 006e2e90, SMSG_DISMOUNT 00741a40, 0x3ad 00741b60, 0x3ae 005744f0, SMSG_VOICE_PARENTAL_CONTROLS 006cfef0, SMSG_GM_MESSAGECHAT 0050ebc0, SMSG_COMMENTATOR_STATE_CHANGED 0056b8a0, SMSG_COMMENTATOR_MAP_INFO 0056bf30, SMSG_COMMENTATOR_PLAYER_INFO 0056b280, SMSG_CLEAR_TARGET 00756800, SMSG_CROSSED_INEBRIATION_THRESHOLD 006d01b0, SMSG_COMPLAINT_RESULT 006e2e90, SMSG_FEATURE_SYSTEM_STATUS 006e2e90, SMSG_CHANNEL_MEMBER_COUNT 004fb540, SMSG_AVAILABLE_VOICE_CHANNEL 006cd0e0, 0x3df 005725c0, SMSG_VOICE_CHAT_STATUS 00500240, SMSG_REPORT_PVP_AFK_RESULT 006e2e90, SMSG_GUILD_BANK_QUERY_RESULTS 005a7250, 0x3ee 005a4800, SMSG_USERLIST_ADD 00504130, SMSG_USERLIST_REMOVE 005042f0, SMSG_USERLIST_UPDATE 00500380, SMSG_INSPECT_TALENT 006ce0c0, SMSG_ECHO_PARTY_SQUELCH 00572610, SMSG_LOOT_LIST 0071ca50, 0x3fd 005cb9f0, 0x3fe 005a4ab0, 0x3ff 005ca6a0, SMSG_MIRROR_IMAGE_COMPONENTED_DATA 007324b0, SMSG_FORCE_DISPLAY_UPDATE 00716cd0, SMSG_IGNORE_DIMINISHING_RETURNS_CHEAT 006e2e90, 0x40a 005a4ae0, SMSG_OVERRIDE_LIGHT 00526530, SMSG_TOTEM_CREATED 00526530, SMSG_SEND_UNLEARN_SPELLS 006e2240, SMSG_PROPOSE_LEVEL_GRANT 00526530, SMSG_REFER_A_FRIEND_FAILURE 00526530, SMSG_MOVE_SPLINE_SET_FLYING 00741c30, SMSG_MOVE_SPLINE_UNSET_FLYING 00741c30, SMSG_SUMMON_CANCEL 006cbcf0, SMSG_ENABLE_BARBER_SHOP 0052f9b0, SMSG_BARBER_SHOP_RESULT 0052e5b0, SMSG_CALENDAR_SEND_CALENDAR 005c3fe0, SMSG_CALENDAR_SEND_EVENT 005c3fe0, SMSG_CALENDAR_FILTER_GUILD 005c3fe0, SMSG_CALENDAR_ARENA_TEAM 005c3fe0, SMSG_CALENDAR_EVENT_INVITE 005c3fe0, SMSG_CALENDAR_EVENT_INVITE_REMOVED 005c3fe0, SMSG_CALENDAR_EVENT_STATUS 005c3fe0, SMSG_CALENDAR_COMMAND_RESULT 006cd380, SMSG_CALENDAR_RAID_LOCKOUT_ADDED 005c3fe0, SMSG_CALENDAR_RAID_LOCKOUT_REMOVED 005c3fe0, SMSG_CALENDAR_EVENT_INVITE_ALERT 005c3fe0, SMSG_CALENDAR_EVENT_INVITE_REMOVED_ALERT 005c3fe0, SMSG_CALENDAR_EVENT_INVITE_STATUS_ALERT 005c3fe0, SMSG_CALENDAR_EVENT_REMOVED_ALERT 005c3fe0, SMSG_CALENDAR_EVENT_UPDATED_ALERT 005c3fe0, SMSG_CALENDAR_EVENT_MODERATOR_STATUS_ALERT 005c3fe0, SMSG_CALENDAR_SEND_NUM_PENDING 005c3fe0, SMSG_NOTIFY_DANCE 00576730, SMSG_PLAY_DANCE 00575ab0, SMSG_STOP_DANCE 00575850, SMSG_DANCE_QUERY_RESPONSE 00635ab0, SMSG_INVALIDATE_DANCE 006354f0, SMSG_LEARNED_DANCE_MOVES 005758a0, 0x45b 00741b00, SMSG_FORCE_PITCH_RATE_CHANGE 00732450, SMSG_MOVE_SPLINE_SET_PITCH_RATE 00741bc0, SMSG_CALENDAR_EVENT_INVITE_NOTES 005c3fe0, SMSG_CALENDAR_EVENT_INVITE_NOTES_ALERT 005c3fe0, SMSG_UPDATE_ACCOUNT_DATA_COMPLETE 006b8fc0, SMSG_TRIGGER_MOVIE 00526530, SMSG_ACHIEVEMENT_EARNED 005b3020, SMSG_CRITERIA_UPDATE 005b3160, SMSG_RESPOND_INSPECT_ACHIEVEMENTS 005b34a0, SMSG_QUEST_UPDATE_ADD_PVP_CREDIT 006d8030, SMSG_CALENDAR_RAID_LOCKOUT_UPDATED 005c3fe0, SMSG_PET_RENAMEABLE 005d3140, SMSG_PHASE_SHIFT_CHANGE 00526530, SMSG_ALL_ACHIEVEMENT_DATA 005b32f0, SMSG_HEALTH_UPDATE 00716d20, SMSG_POWER_UPDATE 007236c0, SMSG_HIGHEST_THREAT_UPDATE 00741c90, SMSG_THREAT_UPDATE 00741c90, SMSG_THREAT_REMOVE 00737b20, SMSG_THREAT_CLEAR 00734b00, SMSG_CONVERT_RUNE 006e2e90, SMSG_RESYNC_RUNES 006e2e90, SMSG_ADD_RUNE_POWER 006e2e90, SMSG_NOTIFY_DEST_LOC_SPELL_CAST 00810050, SMSG_AUCTION_LIST_PENDING_SALES 0059e880, SMSG_MODIFY_COOLDOWN 00804010, SMSG_PET_UPDATE_COMBO_POINTS 005d36a0, SMSG_PRE_RESSURECT 00716d80, SMSG_SERVER_FIRST_ACHIEVEMENT 0050b010, SMSG_PET_LEARNED_SPELLS 005d4c30, SMSG_PET_UNLEARNED_SPELLS 005d4c30, SMSG_CRITERIA_DELETED 005b36f0, SMSG_ACHIEVEMENT_DELETED 005b3610, SMSG_PLAYER_VEHICLE_DATA 00716db0, SMSG_PET_GUIDS 005d6550, SMSG_ITEM_REFUND_INFO_RESPONSE 006defa0, SMSG_ITEM_PURCHASE_REFUND_RESULT 006defa0, SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE 00526530, 0x4bb 005c3fe0, SMSG_LOAD_EQUIPMENT_SET 005af490, 0x4bf 008005a0, 0x4c0 006cd770, 0x4c7 0054b5e0, 0x4c8 006e2e90, 0x4cd 00714ad0, 0x4ce 00732450, 0x4d0 00732450, 0x4d2 00741b60, 0x4d3 00741c30, 0x4d4 00741c30, 0x4d6 005af710, 0x4d8 00714b20, 0x4da 004d92d0, 0x4de 0054b610, 0x4e0 005498c0, 0x4e1 0054b7a0, 0x4e4 0054b680, 0x4e5 0054b750, 0x4e6 005499c0, 0x4e8 0054b7f0, 0x4eb 00526530, 0x4ed 006e2e90, 0x4ee 005ad240, 0x4ef 005ad240, 0x4f1 005ad240, 0x4f7 00526530, 0x4fa 00526530, 0x4fc 008c8de0, 0x4fd 006d84f0, 0x501 005b5190, 0x506 00526530, 0x50a 006e2e90, 0x50b 006e6330, 0x514 006d4110, 0x515 005528d0, 0x516 00732450, 0x518 00741b00, 0x51c 0056bb70, 0x51d 00568420, 0x51e 00716af0

## CVar coverage (CVar::Register)

The reference registers 426 cvars by literal name; whoa registers 376 of them. Missing ones are settings the reference client honours and this one cannot even store.

Missing: CinematicJoystick, ErrorFilter, ErrorLevelMax, ErrorLevelMin, Errors, FootstepSounds, Joystick, ObjectSelectionCircle, POIShiftComplete, ShowErrors, SkyCloudLOD, SoundMemoryCache, Sound_ChaosMode, Sound_EnableDSPEffects, asyncHandlerTimeout, asyncThreadSleep, chatStyle, checkAddonVersion, conversationMode, converted, enableWowMouse, expansionMovie, ffxNetherWorld, ffxRectangle, ffxSpecial, g_accountUsesToken, gxApi, heapAllocTracking, hwDetect, lastCharacterIndex, mouseSpeed, movie, movieSubtitle, pathDistTol, processAffinityMask, realmList, screenshotFormat, screenshotQuality, scriptProfile, shadowCull, shadowInstancing, shadowLOD, shadowScissor, showTimestamps, showfootprintparticles, synchronizeSettings, taintLog, timingMethod, timingTestError, violenceLevel

## Coverage by reference module

Module = the source file named by the reference's own assert strings near the function (linker order); `?` = no anchor before it.

| module | ref fns | bytes | mapped | bytes | stub | verified | spine |
|---|---:|---:|---:|---:|---:|---:|---:|
| DBClient.cpp | 1261 | 274.2k | 2 (0.2%) | 0.2% | 0 | 0 | 81 |
| OggDecompress.cpp | 1504 | 260.7k | 9 (0.6%) | 0.8% | 0 | 0 | 476 |
| ComSatSoundIOSoundEngine.cpp | 975 | 189.6k | 52 (5.3%) | 0.2% | 0 | 0 | 107 |
| Unit_C.cpp | 705 | 182.3k | 3 (0.4%) | 0.5% | 0 | 0 | 294 |
| Player_C.cpp | 736 | 148.3k | 8 (1.1%) | 4.1% | 0 | 0 | 163 |
| HealthBar.cpp | 448 | 102.9k | 0 (0.0%) | 0.0% | 0 | 0 | 68 |
| M2Scene.cpp | 283 | 101.1k | 19 (6.7%) | 5.6% | 0 | 0 | 210 |
| CGxDeviceD3d9Ex.cpp | 238 | 98.4k | 5 (2.1%) | 5.6% | 0 | 0 | 1 |
| GameUI.cpp | 491 | 96.4k | 42 (8.6%) | 17.6% | 29 | 0 | 73 |
| Spell_C.cpp | 355 | 86.2k | 10 (2.8%) | 4.8% | 0 | 0 | 121 |
| Tooltip.cpp | 151 | 86.0k | 69 (45.7%) | 26.9% | 51 | 1 | 16 |
| ChatFrame.cpp | 349 | 82.2k | 28 (8.0%) | 2.6% | 0 | 2 | 36 |
| lmemPool.cpp | 342 | 80.2k | 7 (2.0%) | 0.9% | 0 | 0 | 107 |
| Map.cpp | 237 | 77.5k | 1 (0.4%) | 2.6% | 0 | 0 | 143 |
| SpellCast.cpp | 261 | 70.6k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| WorldParam.cpp | 187 | 64.3k | 5 (2.7%) | 3.3% | 0 | 0 | 118 |
| M2Shared.cpp | 66 | 63.3k | 6 (9.1%) | 2.2% | 0 | 0 | 20 |
| fmod_systemi.cpp | 222 | 62.1k | 0 (0.0%) | 0.0% | 0 | 0 | 99 |
| InputControl.cpp | 287 | 61.8k | 18 (6.3%) | 16.4% | 3 | 0 | 115 |
| SoundEngine.cpp | 406 | 59.7k | 14 (3.4%) | 17.4% | 0 | 0 | 58 |
| SEvt.cpp | 242 | 59.1k | 31 (12.8%) | 4.1% | 0 | 0 | 111 |
| CreepTendril.cpp | 1415 | 58.5k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| SpellBookFrame.cpp | 246 | 57.0k | 20 (8.1%) | 7.4% | 7 | 0 | 15 |
| FFXEffects.cpp | 219 | 56.2k | 2 (0.9%) | 2.1% | 0 | 0 | 72 |
| PartyFrame.cpp | 304 | 55.3k | 70 (23.0%) | 31.3% | 56 | 0 | 11 |
| CSimpleAnimScript.cpp | 223 | 51.5k | 6 (2.7%) | 1.9% | 0 | 0 | 8 |
| Minigame_C.cpp | 352 | 50.8k | 34 (9.7%) | 4.7% | 0 | 0 | 68 |
| LFGInfo.cpp | 229 | 47.7k | 11 (4.8%) | 2.7% | 9 | 0 | 7 |
| ScriptEvents.cpp | 225 | 46.9k | 167 (74.2%) | 71.8% | 60 | 0 | 26 |
| CSimpleHyperlinkedFrame.cpp | 198 | 46.2k | 1 (0.5%) | 0.1% | 0 | 0 | 120 |
| ScanDLLGlue.cpp | 184 | 45.1k | 7 (3.8%) | 3.8% | 1 | 0 | 41 |
| CSimpleHTML.cpp | 364 | 44.4k | 194 (53.3%) | 58.7% | 75 | 2 | 0 |
| fmod_codec_it.cpp | 57 | 42.9k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| asiolist.cpp | 172 | 42.5k | 0 (0.0%) | 0.0% | 0 | 0 | 4 |
| DetailDoodad.cpp | 162 | 41.3k | 0 (0.0%) | 0.0% | 0 | 0 | 71 |
| VehicleCamera_C.cpp | 95 | 40.7k | 0 (0.0%) | 0.0% | 0 | 0 | 39 |
| TextureCache.cpp | 252 | 38.9k | 6 (2.4%) | 5.5% | 0 | 0 | 79 |
| TradeSkillFrame.cpp | 163 | 38.8k | 2 (1.2%) | 0.2% | 0 | 0 | 23 |
| framing.c | 125 | 38.8k | 27 (21.6%) | 51.7% | 0 | 0 | 39 |
| DBCache.cpp | 236 | 38.8k | 9 (3.8%) | 3.1% | 0 | 0 | 83 |
| GameObject_C.cpp | 285 | 38.0k | 0 (0.0%) | 0.0% | 0 | 0 | 59 |
| GxuFontMiscClasses.cpp | 156 | 37.5k | 1 (0.6%) | 0.2% | 0 | 0 | 86 |
| MapChunk.cpp | 129 | 37.2k | 0 (0.0%) | 0.0% | 0 | 0 | 85 |
| ConsoleVar.cpp | 243 | 36.1k | 67 (27.6%) | 29.1% | 0 | 0 | 68 |
| CSimpleFrameScript.cpp | 242 | 35.7k | 58 (24.0%) | 32.9% | 21 | 1 | 4 |
| TextureBlob.cpp | 214 | 34.6k | 10 (4.7%) | 9.1% | 0 | 0 | 84 |
| AchievementInfo.cpp | 179 | 33.7k | 2 (1.1%) | 0.2% | 0 | 0 | 0 |
| Calendar.cpp | 109 | 32.4k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| fmod_output_openal.cpp | 57 | 31.2k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| UnitMissileTrajectory_C.cpp | 97 | 31.1k | 0 (0.0%) | 0.0% | 0 | 0 | 61 |
| MinimapFrame.cpp | 78 | 31.1k | 12 (15.4%) | 11.4% | 1 | 0 | 3 |
| XMLTree.cpp | 184 | 31.1k | 45 (24.5%) | 29.1% | 10 | 0 | 31 |
| MapChunkLiquid.cpp | 77 | 30.9k | 0 (0.0%) | 0.0% | 0 | 0 | 47 |
| Client.cpp | 169 | 30.8k | 39 (23.1%) | 35.1% | 0 | 0 | 31 |
| CGlueMgr.cpp | 183 | 28.9k | 42 (23.0%) | 40.2% | 4 | 0 | 1 |
| LoadingScreen.cpp | 201 | 28.7k | 8 (4.0%) | 10.0% | 0 | 0 | 80 |
| fmod_sample_software.cpp | 51 | 28.1k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| CScriptRegion.cpp | 204 | 27.4k | 68 (33.3%) | 41.1% | 26 | 4 | 50 |
| UIMacros.cpp | 139 | 27.2k | 0 (0.0%) | 0.0% | 0 | 0 | 6 |
| CSimpleRender.cpp | 166 | 26.6k | 13 (7.8%) | 21.4% | 0 | 0 | 50 |
| UnitCombatLog_C.cpp | 106 | 26.2k | 1 (0.9%) | 2.1% | 0 | 0 | 33 |
| CSimpleFrame.cpp | 144 | 25.9k | 5 (3.5%) | 13.4% | 0 | 0 | 36 |
| GossipInfo.cpp | 174 | 25.4k | 15 (8.6%) | 5.5% | 1 | 0 | 5 |
| SoundInterface2Internal.cpp | 139 | 25.2k | 14 (10.1%) | 16.9% | 3 | 0 | 13 |
| PaperDollInfoFrame.cpp | 111 | 25.0k | 25 (22.5%) | 23.4% | 19 | 0 | 8 |
| CSimpleAnim.cpp | 141 | 24.4k | 27 (19.1%) | 26.2% | 6 | 2 | 7 |
| TalentInfo.cpp | 141 | 24.1k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| MovementShared.cpp | 85 | 23.5k | 0 (0.0%) | 0.0% | 0 | 0 | 42 |
| fmod_channel_openal.cpp | 52 | 23.2k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| PetNameCache.cpp | 120 | 22.8k | 0 (0.0%) | 0.0% | 0 | 0 | 38 |
| BattlefieldInfo.cpp | 120 | 22.7k | 34 (28.3%) | 29.3% | 18 | 0 | 0 |
| Texture.cpp | 146 | 22.2k | 7 (4.8%) | 10.5% | 0 | 0 | 75 |
| MapMem.cpp | 101 | 21.3k | 0 (0.0%) | 0.0% | 0 | 0 | 63 |
| Cursor.cpp | 117 | 20.7k | 1 (0.9%) | 1.8% | 0 | 0 | 19 |
| ObjectEffect.cpp | 81 | 20.5k | 0 (0.0%) | 0.0% | 0 | 0 | 33 |
| FriendList.cpp | 92 | 20.5k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| CSimpleEditBox.cpp | 94 | 20.4k | 5 (5.3%) | 9.2% | 0 | 0 | 0 |
| CSimpleMovieFrame.cpp | 128 | 19.6k | 32 (25.0%) | 28.4% | 5 | 1 | 2 |
| CheckExecutableSignature.cpp | 95 | 19.5k | 1 (1.1%) | 1.1% | 0 | 0 | 29 |
| BattlenetLogin.cpp | 111 | 19.4k | 1 (0.9%) | 0.0% | 0 | 0 | 0 |
| TaxiMapFrame.cpp | 97 | 19.4k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| Profile.cpp | 128 | 19.1k | 1 (0.8%) | 2.3% | 0 | 0 | 28 |
| DressUpModelFrame.cpp | 116 | 18.8k | 10 (8.6%) | 12.8% | 0 | 0 | 7 |
| AddOns.cpp | 101 | 18.1k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| KnowledgeBase.cpp | 119 | 17.8k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| MailInfo.cpp | 77 | 17.8k | 5 (6.5%) | 2.4% | 4 | 0 | 7 |
| DeclinedWords.cpp | 72 | 17.4k | 7 (9.7%) | 20.8% | 4 | 0 | 32 |
| OsClipboard.cpp | 74 | 16.8k | 1 (1.4%) | 3.4% | 0 | 0 | 45 |
| QuestTextParser.cpp | 87 | 16.5k | 5 (5.7%) | 1.6% | 0 | 0 | 33 |
| Item_C.cpp | 107 | 16.4k | 0 (0.0%) | 0.0% | 0 | 0 | 24 |
| QuestLog.cpp | 80 | 16.4k | 4 (5.0%) | 4.7% | 2 | 0 | 2 |
| CGxDevice.cpp | 54 | 16.2k | 4 (7.4%) | 3.8% | 0 | 0 | 9 |
| tga.cpp | 82 | 16.2k | 0 (0.0%) | 0.0% | 0 | 0 | 23 |
| AuctionHouse.cpp | 49 | 16.1k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| fmod_codec_dls.cpp | 36 | 16.0k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| Grunt.cpp | 69 | 15.9k | 2 (2.9%) | 6.4% | 0 | 0 | 5 |
| fmod_plugin.cpp | 39 | 15.7k | 0 (0.0%) | 0.0% | 0 | 0 | 15 |
| ComSatClient.cpp | 86 | 15.6k | 5 (5.8%) | 3.8% | 5 | 0 | 6 |
| SComp.cpp | 83 | 15.1k | 1 (1.2%) | 1.2% | 0 | 0 | 2 |
| PetInfo.cpp | 69 | 14.8k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| ActionBarFrame.cpp | 64 | 14.7k | 18 (28.1%) | 19.9% | 4 | 0 | 8 |
| PlayerName.cpp | 71 | 14.6k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| UIMacroOptions.cpp | 93 | 14.4k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| fmod_dsp_pitchshift.cpp | 52 | 14.4k | 0 (0.0%) | 0.0% | 0 | 0 | 5 |
| fmod_codec_wav_riff.cpp | 46 | 14.1k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| GxuFontUtil.cpp | 60 | 13.3k | 2 (3.3%) | 1.0% | 0 | 0 | 45 |
| EffectGlow.cpp | 64 | 13.1k | 2 (3.1%) | 6.6% | 0 | 0 | 5 |
| blp.cpp | 94 | 13.0k | 12 (12.8%) | 12.2% | 0 | 0 | 20 |
| fmod_output_wasapi.cpp | 68 | 12.8k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| ObjectMgrClient.cpp | 89 | 12.8k | 4 (4.5%) | 6.5% | 0 | 0 | 23 |
| VehiclePassenger_C.cpp | 49 | 12.7k | 0 (0.0%) | 0.0% | 0 | 0 | 17 |
| fmod_dsp_echo.cpp | 55 | 12.5k | 0 (0.0%) | 0.0% | 0 | 0 | 8 |
| GuildBankFrame.cpp | 71 | 12.4k | 9 (12.7%) | 8.8% | 2 | 0 | 13 |
| fmod_os_cdda.cpp | 30 | 12.4k | 0 (0.0%) | 0.0% | 0 | 0 | 14 |
| SLock.cpp | 68 | 12.3k | 1 (1.5%) | 2.0% | 0 | 1 | 18 |
| CGxD3d9ExTexture.cpp | 47 | 12.2k | 0 (0.0%) | 0.0% | 0 | 0 | 5 |
| CharacterCreation.cpp | 56 | 12.2k | 19 (33.9%) | 32.6% | 3 | 0 | 3 |
| aSfxDsp.cpp | 48 | 12.1k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| EvtSched.cpp | 73 | 12.1k | 2 (2.7%) | 6.9% | 0 | 0 | 33 |
| SoundInterface2ZoneSounds.cpp | 68 | 11.6k | 0 (0.0%) | 0.0% | 0 | 0 | 23 |
| UIBindings.cpp | 59 | 11.5k | 16 (27.1%) | 32.4% | 15 | 0 | 4 |
| CSimpleMessageScrollFrame.cpp | 75 | 11.4k | 6 (8.0%) | 9.2% | 0 | 0 | 0 |
| fmod_codec_mpeg.cpp | 43 | 11.3k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| ContainerFrame.cpp | 32 | 11.2k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| EquipmentManager.cpp | 55 | 10.8k | 1 (1.8%) | 0.3% | 0 | 0 | 3 |
| ConsoleClient.cpp | 52 | 10.3k | 5 (9.6%) | 10.9% | 0 | 0 | 6 |
| SBig.cpp | 54 | 10.2k | 0 (0.0%) | 0.0% | 0 | 0 | 36 |
| WowConnection.cpp | 44 | 10.1k | 1 (2.3%) | 2.3% | 0 | 0 | 4 |
| RaidInfo.cpp | 33 | 10.1k | 12 (36.4%) | 32.1% | 9 | 0 | 1 |
| CurrencyTypes.cpp | 48 | 10.1k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| MapLoad.cpp | 29 | 9.9k | 1 (3.4%) | 3.3% | 0 | 0 | 22 |
| fmod_output_dsound.cpp | 36 | 9.9k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| fmod_codec_s3m.cpp | 14 | 9.7k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| fmod_codec.cpp | 43 | 9.2k | 0 (0.0%) | 0.0% | 0 | 0 | 9 |
| fmod_codec_fsb.cpp | 18 | 9.2k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| WardenClient.cpp | 62 | 8.9k | 1 (1.6%) | 9.2% | 0 | 0 | 0 |
| TradeFrame.cpp | 43 | 8.8k | 7 (16.3%) | 24.3% | 6 | 0 | 5 |
| vorbisfile.c | 23 | 8.6k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| DanceStudio.cpp | 53 | 8.6k | 0 (0.0%) | 0.0% | 0 | 0 | 24 |
| DuelInfo.cpp | 49 | 8.4k | 1 (2.0%) | 0.4% | 0 | 0 | 4 |
| AccountData.cpp | 47 | 8.4k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| GuildInfo.cpp | 35 | 8.3k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| MerchantFrame.cpp | 41 | 8.3k | 1 (2.4%) | 2.8% | 1 | 0 | 4 |
| fmod_pluginfactory.cpp | 38 | 8.3k | 0 (0.0%) | 0.0% | 0 | 0 | 23 |
| MapObjRead.cpp | 37 | 8.1k | 0 (0.0%) | 0.0% | 0 | 0 | 11 |
| fmod_codec_oggvorbis.cpp | 31 | 7.9k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| CalendarEvent.cpp | 40 | 7.9k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| ChatBubbleFrame.cpp | 55 | 7.6k | 0 (0.0%) | 0.0% | 0 | 0 | 9 |
| RealmList.cpp | 46 | 7.5k | 18 (39.1%) | 35.7% | 0 | 0 | 0 |
| fmod_sample_openal.cpp | 19 | 7.4k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| PetitionVendor.cpp | 38 | 6.9k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| Vehicle_C.cpp | 43 | 6.8k | 0 (0.0%) | 0.0% | 0 | 0 | 19 |
| Corpse_C.cpp | 63 | 6.8k | 0 (0.0%) | 0.0% | 0 | 0 | 14 |
| cmemblock.cpp | 46 | 6.7k | 10 (21.7%) | 19.4% | 0 | 0 | 16 |
| SSignature.cpp | 36 | 6.7k | 6 (16.7%) | 11.8% | 0 | 0 | 18 |
| SCmd.cpp | 55 | 6.6k | 4 (7.3%) | 1.1% | 0 | 0 | 16 |
| fmod_dsp_itecho.cpp | 35 | 6.6k | 0 (0.0%) | 0.0% | 0 | 0 | 3 |
| UnitCombat_C.cpp | 25 | 6.5k | 0 (0.0%) | 0.0% | 0 | 0 | 9 |
| hidmanagerimpl.cpp | 34 | 6.4k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| ItemSocketInfo.cpp | 56 | 6.1k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| ClientServices.cpp | 37 | 6.1k | 1 (2.7%) | 0.6% | 0 | 0 | 0 |
| EZ_LCD_Page.cpp | 69 | 6.1k | 0 (0.0%) | 0.0% | 0 | 0 | 4 |
| UnitSound_C.cpp | 37 | 6.0k | 0 (0.0%) | 0.0% | 0 | 0 | 13 |
| UnitVehicle_C.cpp | 36 | 6.0k | 0 (0.0%) | 0.0% | 0 | 0 | 21 |
| NetClient.cpp | 44 | 5.9k | 1 (2.3%) | 0.4% | 0 | 0 | 6 |
| LootFrame.cpp | 36 | 5.8k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| block.c | 16 | 5.8k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| EZ_LCD.cpp | 54 | 5.7k | 0 (0.0%) | 0.0% | 0 | 0 | 8 |
| NamePlateFrame.cpp | 10 | 5.6k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| OsURLDownload.cpp | 21 | 5.5k | 0 (0.0%) | 0.0% | 0 | 0 | 5 |
| GMTicketInfo.cpp | 37 | 5.3k | 6 (16.2%) | 14.4% | 0 | 0 | 1 |
| fmod.cpp | 110 | 5.2k | 0 (0.0%) | 0.0% | 0 | 0 | 42 |
| fmod_codec_mod.cpp | 10 | 5.0k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| fmod_thread.cpp | 25 | 4.9k | 0 (0.0%) | 0.0% | 0 | 0 | 16 |
| fmod_dsp_sfxreverb.cpp | 30 | 4.8k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| fmod_file.cpp | 44 | 4.8k | 0 (0.0%) | 0.0% | 0 | 0 | 17 |
| fmod_file_net.cpp | 8 | 4.7k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| ClassTrainerFrame.cpp | 18 | 4.7k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| WDataStore.cpp | 33 | 4.6k | 0 (0.0%) | 0.0% | 0 | 0 | 15 |
| ObjectAlloc.cpp | 41 | 4.6k | 3 (7.3%) | 4.1% | 0 | 0 | 20 |
| GruntLogin.cpp | 21 | 4.6k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| Path.cpp | 41 | 4.6k | 0 (0.0%) | 0.0% | 0 | 0 | 18 |
| LootRoll.cpp | 30 | 4.6k | 0 (0.0%) | 0.0% | 0 | 0 | 9 |
| CGxDeviceOpenGl.cpp | 25 | 4.5k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| Bag_C.cpp | 34 | 4.5k | 0 (0.0%) | 0.0% | 0 | 0 | 12 |
| MapArea.cpp | 21 | 4.5k | 0 (0.0%) | 0.0% | 0 | 0 | 11 |
| ReputationInfo.cpp | 40 | 4.5k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| fmod_codec_tag.cpp | 7 | 4.4k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| ArenaTeamInfo.cpp | 33 | 4.3k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| CSimpleMessageFrame.cpp | 29 | 4.2k | 2 (6.9%) | 3.5% | 0 | 0 | 0 |
| OsTcp.cpp | 23 | 4.2k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| ShaderEffectManager.cpp | 24 | 4.1k | 1 (4.2%) | 11.9% | 0 | 0 | 3 |
| OsVersionHash.cpp | 19 | 4.1k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| CharacterComponent.cpp | 11 | 4.0k | 1 (9.1%) | 28.8% | 0 | 0 | 5 |
| RCString.cpp | 46 | 4.0k | 2 (4.3%) | 5.9% | 0 | 0 | 17 |
| OsSecureRandom.cpp | 33 | 3.9k | 0 (0.0%) | 0.0% | 0 | 0 | 2 |
| fmod_string.cpp | 18 | 3.9k | 0 (0.0%) | 0.0% | 0 | 0 | 14 |
| fmod_output.cpp | 20 | 3.9k | 0 (0.0%) | 0.0% | 0 | 0 | 7 |
| SoundInterface2VoiceChat.cpp | 44 | 3.9k | 0 (0.0%) | 0.0% | 0 | 0 | 13 |
| CSimpleFont.cpp | 25 | 3.8k | 2 (8.0%) | 35.5% | 0 | 0 | 1 |
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
| BattlenetUI.cpp | 31 | 2.8k | 1 (3.2%) | 1.8% | 0 | 0 | 6 |
| StableInfo.cpp | 15 | 2.7k | 1 (6.7%) | 1.3% | 0 | 0 | 0 |
| fmod_output_wavwriter_nrt.cpp | 23 | 2.7k | 0 (0.0%) | 0.0% | 0 | 0 | 4 |
| sharedbook.c | 8 | 2.7k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| fmod_codec_flac.cpp | 22 | 2.6k | 0 (0.0%) | 0.0% | 0 | 0 | 1 |
| floor1.c | 7 | 2.6k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| PetitionInfo.cpp | 20 | 2.5k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
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
| CharacterModelBase.cpp | 24 | 2.0k | 6 (25.0%) | 28.4% | 2 | 0 | 0 |
| floor0.c | 13 | 2.0k | 0 (0.0%) | 0.0% | 0 | 0 | 0 |
| ? | 15 | 0.8k | 3 (20.0%) | 47.8% | 0 | 0 | 7 |

## Next to port: unmapped, on the world spine (by callers x size)

| addr | module | size | callers | whoa | strings |
|---|---|---:|---:|---|---|
| 00842da0 | M2Shared.cpp? | 17797 | 70 |  |  |
| 00509dd0 | ChatFrame.cpp | 4047 | 126 |  | !@#$%^&*, %d. %s |
| 00832ea0 | M2Scene.cpp? | 5663 | 42 |  | "%s", %s = %g, "%s", %s = %g, %s = %g |
| 00808200 | Spell_C.cpp | 2446 | 98 |  | %s_PET, .\Spell_C.cpp |
| 007385c0 | Unit_C.cpp | 4083 | 57 |  | .\Unit_C.cpp |
| 0061fec0 | Tooltip.cpp | 756 | 204 |  | %sTextLeft%d, %sTextRight%d |
| 0067ca30 | DBCache.cpp | 412 | 275 |  | .?AUDBCACHECALLBACK@@ |
| 00745230 | Unit_C.cpp? | 2889 | 29 |  |  |
| 006f61d0 | ObjectEffect.cpp | 579 | 125 |  |  |
| 00821a20 | M2Scene.cpp? | 5621 | 11 |  |  |
| 0082f0f0 | M2Scene.cpp? | 6267 | 7 |  |  |
| 004c1f00 | TextureBlob.cpp? | 533 | 91 |  |  |
| 0072a000 | Unit_C.cpp | 651 | 69 |  | .\Unit_C.cpp, UNKNOWNOBJECT |
| 005dd5a0 | TradeSkillFrame.cpp | 2894 | 14 |  | .PBVSkillLineAbilityRec@@, .\TradeSkillFrame.cpp |
| 00519280 | GameUI.cpp | 513 | 83 |  | .\GameUI.cpp, INTERFACESOUND_CURSORDROPOBJECT |
| 008d67d0 | fmod_systemi.cpp | 7647 | 4 |  | ..\..\src\fmod_systemi.cpp, TITLE |
| 00621070 | Tooltip.cpp | 5010 | 6 |  |  - %s, %s - %s |
| 004cfd20 | SoundInterface2Internal.cpp? | 101 | 345 |  |  |
| 005fbbc0 | InputControl.cpp | 588 | 53 |  | .\InputControl.cpp |
| 00729740 | Unit_C.cpp | 815 | 37 |  |  |
| 00464580 | TextureBlob.cpp | 182 | 161 |  | .\TextureBlob.cpp |
| 007413f0 | Unit_C.cpp | 695 | 41 |  |  |
| 00771d10 | SSignature.cpp? | 2374 | 11 |  | 
%s

,  : error %u:  |
| 004b9760 | Texture.cpp? | 416 | 67 |  | fileName, fileName[0] |
| 0067de90 | DBCache.cpp | 412 | 67 |  | .?AUDBCACHECALLBACK@@ |
| 0080cce0 | Spell_C.cpp | 3395 | 7 |  | .\Spell_C.cpp, GAMEABILITYACTIVATE |
| 00681f60 | DBCache.cpp? | 452 | 53 |  | "%s", %s = %g, "%s", %s = %g, %s = %g |
| 00736d30 | Unit_C.cpp | 923 | 24 |  | .\Unit_C.cpp |
| 0053cf10 | SpellBookFrame.cpp | 646 | 34 |  | d:\BuildServer\WoW\1\work\WoW-code\branc |
| 007251c0 | Unit_C.cpp | 798 | 27 |  |  |
| 004ba060 | Texture.cpp? | 260 | 84 |  | AsyncFileReadWait(): s_waiting != FALSE, AsyncFileReadWait():%s
 |
| 00486b20 | CSimpleRender.cpp? | 778 | 26 |  |  |
| 0041d770 | OggDecompress.cpp | 199 | 103 |  |  |
| 0082ced0 | M2Scene.cpp? | 706 | 28 |  |  |
| 00708c20 | Item_C.cpp | 1998 | 9 |  | .\Item_C.cpp, SPELL_FAILED_NO_CHARGES_REMAIN |
| 006b0b50 | blp.cpp? | 41 | 465 |  |  |
| 008cfda0 | fmod_memory.cpp | 388 | 48 |  | ..\..\src\fmod_memory.cpp |
| 00802f80 | Spell_C.cpp | 1396 | 12 |  | .\Spell_C.cpp |
| 004922f0 | CSimpleFrame.cpp | 951 | 18 |  |  |
| 00603330 | InputControl.cpp? | 1379 | 12 |  | d:\BuildServer\WoW\1\work\WoW-code\branc |

## Next to port: unmapped, anywhere

| addr | module | size | callers | whoa | strings |
|---|---|---:|---:|---|---|
| 00842da0 | M2Shared.cpp? | 17797 | 70 |  |  |
| 006277f0 | Tooltip.cpp | 24814 | 42 |  | 			<setspelldesc_%d>, 			<setspelldesc_%d></setspelldesc_%d>
 |
| 00509dd0 | ChatFrame.cpp | 4047 | 126 |  | !@#$%^&*, %d. %s |
| 00832ea0 | M2Scene.cpp? | 5663 | 42 |  | "%s", %s = %g, "%s", %s = %g, %s = %g |
| 00808200 | Spell_C.cpp | 2446 | 98 |  | %s_PET, .\Spell_C.cpp |
| 007385c0 | Unit_C.cpp | 4083 | 57 |  | .\Unit_C.cpp |
| 006918f0 | CGxDeviceD3d9Ex.cpp? | 1531 | 113 |  |  |
| 0061fec0 | Tooltip.cpp | 756 | 204 |  | %sTextLeft%d, %sTextRight%d |
| 006238a0 | Tooltip.cpp | 6714 | 19 |  | %d-%d, %s (%d) |
| 0067ca30 | DBCache.cpp | 412 | 275 |  | .?AUDBCACHECALLBACK@@ |
| 00745230 | Unit_C.cpp? | 2889 | 29 |  |  |
| 006f61d0 | ObjectEffect.cpp | 579 | 125 |  |  |
| 00821a20 | M2Scene.cpp? | 5621 | 11 |  |  |
| 0082f0f0 | M2Scene.cpp? | 6267 | 7 |  |  |
| 004c1f00 | TextureBlob.cpp? | 533 | 91 |  |  |
| 00695fd0 | CGxDeviceD3d9Ex.cpp? | 24484 | 1 |  |  |
| 0072a000 | Unit_C.cpp | 651 | 69 |  | .\Unit_C.cpp, UNKNOWNOBJECT |
| 0093cd10 | fmod_output_dsound.cpp? | 2731 | 15 |  |  |
| 005dd5a0 | TradeSkillFrame.cpp | 2894 | 14 |  | .PBVSkillLineAbilityRec@@, .\TradeSkillFrame.cpp |
| 00519280 | GameUI.cpp | 513 | 83 |  | .\GameUI.cpp, INTERFACESOUND_CURSORDROPOBJECT |
| 00546310 | SpellBookFrame.cpp? | 1486 | 25 |  | d:\BuildServer\WoW\1\work\WoW-code\branc |
| 008d67d0 | fmod_systemi.cpp | 7647 | 4 |  | ..\..\src\fmod_systemi.cpp, TITLE |
| 00621070 | Tooltip.cpp | 5010 | 6 |  |  - %s, %s - %s |
| 004cfd20 | SoundInterface2Internal.cpp? | 101 | 345 |  |  |
| 005fbbc0 | InputControl.cpp | 588 | 53 |  | .\InputControl.cpp |
| 00729740 | Unit_C.cpp | 815 | 37 |  |  |
| 009411a0 | fmod_sample_software.cpp? | 7717 | 3 |  |  |
| 00464580 | TextureBlob.cpp | 182 | 161 |  | .\TextureBlob.cpp |
| 009b1b40 | SpellCast.cpp? | 5858 | 4 |  | CDATA, ENTITIES |
| 007413f0 | Unit_C.cpp | 695 | 41 |  |  |
| 00771d10 | SSignature.cpp? | 2374 | 11 |  | 
%s

,  : error %u:  |
| 004b9760 | Texture.cpp? | 416 | 67 |  | fileName, fileName[0] |
| 0067de90 | DBCache.cpp | 412 | 67 |  | .?AUDBCACHECALLBACK@@ |
| 009013b0 | fmod_codec_it.cpp | 9335 | 2 |  | ..\..\src\fmod_codec_it.cpp, FMOD IT final mixdown unit |
| 0080cce0 | Spell_C.cpp | 3395 | 7 |  | .\Spell_C.cpp, GAMEABILITYACTIVATE |
| 008a65e0 | ComSatSoundIOSoundEngine.cpp? | 6621 | 3 |  |  |
| 009567a0 | asiolist.cpp? | 374 | 67 |  |  |
| 00681f60 | DBCache.cpp? | 452 | 53 |  | "%s", %s = %g, "%s", %s = %g, %s = %g |
| 0093f9b0 | fmod_sample_software.cpp? | 6100 | 3 |  |  |
| 00492770 | CSimpleFrame.cpp | 1133 | 20 |  |  |

## Mapped but stubbed (WHOA_UNIMPLEMENTED)

| addr | module | size | callers | whoa | strings |
|---|---|---:|---:|---|---|
| 005e95c0 | PaperDollInfoFrame.cpp | 1501 | 0 | `Script_GetInventoryItemsForSlot` [table] | .\PaperDollInfoFrame.cpp, Usage: GetInventoryItemsForSlot(slot [,  |
| 0062dae0 | Tooltip.cpp | 1382 | 0 | `CGTooltip_SetHyperlink` [table] | %I64X:, %s:SetHyperlink(): Unknown link type |
| 0062e050 | Tooltip.cpp | 1226 | 0 | `CGTooltip_SetInventoryItem` [table] | Invalid inventory slot in SetInventoryIt, Usage: %s:SetInventoryItem(unit, slot [, |
| 004a15a0 | CSimpleFrameScript.cpp | 1022 | 0 | `CSimpleFrame_SetBackdrop` [table] | .\CSimpleFrameScript.cpp, Usage: %s:SetBackdrop(nil or {bgFile = " |
| 00573690 | RaidInfo.cpp | 975 | 0 | `Script_GetRaidRosterInfo` [table] | .\RaidInfo.cpp, MAINASSIST |
| 0062f420 | Tooltip.cpp | 800 | 0 | `CGTooltip_SetBagItem` [table] | .\Tooltip.cpp, d:\BuildServer\WoW\1\work\WoW-code\branc |
| 00537240 | PartyFrame.cpp? | 719 | 0 | `Script_BNGetFOFInfo` [table] | BNUI: BNGetFOFInfo for ID %u index %d is, Incorrect ID |
| 004a12d0 | CSimpleFrameScript.cpp | 718 | 0 | `CSimpleFrame_GetBackdrop` [table] | bgFile, bottom |
| 005879d0 | TradeFrame.cpp | 656 | 0 | `Script_ClickTradeButton` [table] | .\TradeFrame.cpp, Usage: ClickTradeButton(index) |
| 0062eae0 | Tooltip.cpp | 640 | 0 | `CGTooltip_SetTradeSkillItem` [table] | Invalid trade skill item in SetTradeSkil |
| 00610550 | ScriptEvents.cpp | 625 | 0 | `Script_UnitRangedDamage` [table] | Usage: UnitRangedDamage("unit") |
| 0062fcf0 | Tooltip.cpp | 621 | 0 | `CGTooltip_SetAuctionItem` [table] | Usage: %s:SetAuctionItem("type", index), bidder |
| 00587c60 | TradeFrame.cpp | 589 | 0 | `Script_GetTradeTargetItemInfo` [table] | %s%s%s, Usage: GetTradeTargetItemInfo(index) |
| 00574ab0 | RaidInfo.cpp | 579 | 0 | `Script_SetRaidTarget` [table] | .\RaidInfo.cpp, Usage: SetRaidTarget(unit, index) |
| 0052e1b0 | PartyFrame.cpp | 578 | 0 | `Script_SetPartyAssignment` [table] | .\PartyFrame.cpp, Invalid Party assignment |
| 0062f1e0 | Tooltip.cpp | 571 | 0 | `CGTooltip_SetTradeTargetItem` [table] | Invalid trade slot in SetTradeTargetItem |
| 0062f9e0 | Tooltip.cpp | 569 | 0 | `CGTooltip_SetInboxItem` [table] | Usage: %s:SetInboxItem(messageIndex, att |
| 0053a300 | PartyFrame.cpp? | 568 | 0 | `Script_BNListConversation` [table] | %s%s%s, PLAYER_LIST_DELIMITER |
| 0052dc20 | PartyFrame.cpp | 564 | 0 | `Script_SetLootMethod` [table] | Invalid loot method, Usage: SetLootMethod("method" [,master]) |
| 00539d70 | PartyFrame.cpp? | 530 | 0 | `Script_BNGetFriendToonInfo` [table] | Couldn't find a toon at friend index %d,, Couldn't find a toon at friend index %d, |
| 00587eb0 | TradeFrame.cpp | 523 | 0 | `Script_GetTradePlayerItemInfo` [table] | %s%s%s, .\TradeFrame.cpp |
| 0054de00 | BattlefieldInfo.cpp | 519 | 0 | `Script_SortBattlefieldScoreData` [table] | Usgae: SortBattlefieldScoreData("type"), class |
| 0054c4d0 | BattlefieldInfo.cpp | 516 | 0 | `Script_GetBattlefieldVehicleInfo` [table] | .\BattlefieldInfo.cpp, Usage: GetBattlefieldVehicleInfo(index) |
| 005e9e40 | PaperDollInfoFrame.cpp | 512 | 0 | `Script_GetInventoryItemCount` [table] |  |
| 00535180 | PartyFrame.cpp? | 510 | 0 | `Script_BNGetFriendInviteInfo` [table] | BNUI: Invite Info Account name: %s %s, BNUI: Invite Info ID: %u |
| 00972210 | CSimpleHTML.cpp? | 502 | 0 | `CSimpleScrollFrame_SetScrollChild` [table] | %s:SetScrollChild(): Couldn't find 'this, %s:SetScrollChild(): Couldn't find frame |
| 0062eff0 | Tooltip.cpp | 496 | 0 | `CGTooltip_SetTradePlayerItem` [table] | .\Tooltip.cpp, Invalid trade slot in SetTradePlayerItem |
| 0054c2e0 | BattlefieldInfo.cpp | 496 | 0 | `Script_GetBattlefieldPosition` [table] | .\BattlefieldInfo.cpp, Usage: GetBattlefieldPosition(index) |
| 0062e900 | Tooltip.cpp | 480 | 0 | `CGTooltip_SetTrainerService` [table] | Invalid trainer service in SetTrainerSer |
| 0061ef10 | Tooltip.cpp | 480 | 0 | `CGTooltip_GetItem` [table] | .\Tooltip.cpp |
| 00536e40 | PartyFrame.cpp? | 457 | 0 | `Script_BNReportPlayer` [table] | ABUSE, BNET_REPORT_SENT |
| 00535ce0 | PartyFrame.cpp? | 452 | 0 | `Script_BNCreateConversation` [table] | Usage: BNCreateConversation(id,id) |
| 00630620 | Tooltip.cpp | 443 | 0 | `CGTooltip_SetCurrencyToken` [table] | .\Tooltip.cpp, Usage: %s:SetCurrencyToken(index) |
| 00625470 | Tooltip.cpp | 437 | 0 | `CGTooltip_SetPetAction` [table] | Usage: %s:SetPetAction(slot) |
| 00535aa0 | PartyFrame.cpp? | 435 | 0 | `Script_BNGetCustomMessageTable` [table] | Usage: BNGetCustomMessageTable(table) |
| 005e9bc0 | PaperDollInfoFrame.cpp | 433 | 0 | `Script_GetInventoryItemTexture` [table] | %s%s%s |
| 0049e350 | CScriptRegionScript.cpp | 431 | 0 | `CScriptRegion_CreateAnimationGroup` [table] | %s:CreateAnimationGroup(): Couldn't find, %s:CreateAnimationGroup(): Recursively i |
| 00631b60 | Tooltip.cpp? | 430 | 0 | `CGTooltip_SetHyperlinkCompareItem` [table] | Usage: %s:SetHyperlinkCompareItem("hyper, d:\BuildServer\WoW\1\work\WoW-code\branc |
| 006307e0 | Tooltip.cpp | 427 | 0 | `CGTooltip_SetBackpackToken` [table] | .\Tooltip.cpp, Usage: %s:SetBackpackToken(index) |
| 00613f90 | ScriptEvents.cpp | 426 | 0 | `Script_GetPlayerInfoByGUID` [table] | Usage: GetPlayerInfoByGUID("playerGUID") |

## Divergence smells: reference strings the whoa counterpart never mentions

A reference function that formats, asserts or looks up a string its port does not is missing a branch, an error path or a data lookup. Top 40 by count.

| addr | whoa | missing |
|---|---|---|
| 0087c710 | `SESound::Init` |  - %d Channels Requested.;  - %d Output drivers detected;  - DSPBufferSize = %d [Valid values are 0 = AUTO D;  - DSPBufferSize = AUTO DETECT |
| 0096e9c0 | `StringToClickAction` | AnyDown; AnyUp; Button10Down; Button10Up |
| 0051d9b0 | `CGGameUI::RegisterGameCVars` | Automatically loot items when the loot window open; Clear the target when clicking on terrain; Enables the equipment management UI; How long to display Battle.net toast windows, in s |
| 004d1600 | `SI2::RegisterUserCVars` |  - ========= PLAYBACK =========;  - ========== VOLUME ==========;  - =========== MISC ===========;  - Ambience Volume       [%.2f] |
| 00401b60 | `ClientRegisterConsoleCommands` | %1.1f; Account Type; Check interface addon version number; ErrorFilter |
| 0061eb40 | `CGTooltip_SetOwner` | %s:SetOwner(): Can't set owner to self; %s:SetOwner(): Couldn't find 'this' in frame objec; %s:SetOwner(): Wrong object type, expected frame; ANCHOR_BOTTOM |
| 004dab40 | `CGlueMgr::PollAccountLogin` | %d%d%d%d%b%d; %d%d%d%d%d%d%d%d%d%d; %s\n%s; CHANGE_REALM |
| 004067f0 | `InitializeGlobal` | .PAD; Database compression; Desired method for game timing; Error reported by the timing validation system |
| 00405ab0 | `BuildPatchArchiveList` | ..\Data\; ..\Data\%s\; Data\; Data\%s\ |
| 0079e7c0 | `CMap::MapMemInitialize` | CMap::lowDetailIndexPool; Shaders\Vertex; Terrain; Terrain0 |
| 0061d3d0 | `CGTooltip_SetAnchorType` | ANCHOR_BOTTOM; ANCHOR_BOTTOMLEFT; ANCHOR_BOTTOMRIGHT; ANCHOR_CURSOR |
| 00405dd0 | `Sub405DD0` | Country; Data\; Failed to open archive %s.; Failed to read data from the network. Please check |
| 00403910 | `TransferAbortedHandler` | TRANSFER_ABORT_DIFFICULTY%d; TRANSFER_ABORT_ERROR; TRANSFER_ABORT_INSUF_EXPAN_LVL%d; TRANSFER_ABORT_MAP_NOT_ALLOWED |
| 006e2e90 | `InventoryChangeFailureHandler` | %d%d%d%d%d%d%d%d%d; %s%s%s%s%s%s; COMPLAINT_ADDED; Godmode disabled |
| 0062dae0 | `CGTooltip_SetHyperlink` | %I64X:; %s:SetHyperlink(): Unknown link type; Usage: %s:SetHyperlink(link); achievement: |
| 0061d650 | `CGTooltip_GetAnchorType` | ANCHOR_BOTTOM; ANCHOR_BOTTOMLEFT; ANCHOR_BOTTOMRIGHT; ANCHOR_CURSOR |
| 008ce200 | `FindPatchPrefix_SC2_ArchiveName` | <unknown>; Grunt; My public address is %s; My realm ID is %d |
| 0060a630 | `Script_GetGUIDFromString` | arena%d; arenapet%d; d:\BuildServer\WoW\1\work\WoW-code\branches\wow-pa; party%d |
| 005a8f10 | `Script_GetActionInfo` | CRITTER; MOUNT; UNKNOWN; Usage: GetActionInfo(slot) |
| 0052dc20 | `Script_SetLootMethod` | Invalid loot method; Usage: SetLootMethod("method" [,master]); d:\BuildServer\WoW\1\work\WoW-code\branches\wow-pa; freeforall |
| 004a15a0 | `CSimpleFrame_SetBackdrop` | Usage: %s:SetBackdrop(nil or {bgFile = "bgFile", e; bgFile; bottom; edgeFile |
| 00515200 | `ActionTypeName` | CRITTER; MOUNT; UNKNOWN; companion |
| 004a12d0 | `CSimpleFrame_GetBackdrop` | bgFile; bottom; edgeFile; edgeSize |
| 0069ed50 | `CGxDeviceGLL::PatchVertexShader` |  = program.env  ;  = program.local;  = { program.env  ;  = { program.local |
| 00631000 | `CGTooltip_SetAction` | ATTACK; PET_ACTION_%s; PET_MODE_%s; UberTooltips |
| 0054de00 | `Script_SortBattlefieldScoreData` | Usgae: SortBattlefieldScoreData("type"); class; damage; deaths |
| 00537240 | `Script_BNGetFOFInfo` | BNUI: BNGetFOFInfo for ID %u index %d is ID %u, Ac; Incorrect ID; Is not; Must select mutual and/or non. |
| 00536e40 | `Script_BNReportPlayer` | ABUSE; BNET_REPORT_SENT; Report note is too long.; THREAT |
| 00535180 | `Script_BNGetFriendInviteInfo` | BNUI: Invite Info Account name: %s %s; BNUI: Invite Info ID: %u; BNUI: Invite Info message: %s; BNUI: Invite Info time: %d |
| 005343f0 | `Script_BNGetInfo` | BNUI: GetInfo AFK is %d; BNUI: GetInfo DND is %d; BNUI: GetInfo account ID is %u; BNUI: GetInfo custom message is %s |
| 0052e1b0 | `Script_SetPartyAssignment` | Invalid Party assignment; MAINASSIST; MAINTANK; SetPartyAssignment |
| 0054c8a0 | `Script_GetWorldPVPQueueStatus` | Usage: GetWorldPVPQueueStatus(index); active; confirm; error |
| 00539d70 | `Script_BNGetFriendToonInfo` | Couldn't find a toon at friend index %d, online to; Couldn't find a toon at friend index %d, toon inde; Friend index %d too large, only %d friends.; Toon index %d too large, only %d toons. |
| 0052cd90 | `Script_GetLootMethod` | ERROR!; freeforall; master; needbeforegreed |
| 0052a980 | `CGGameUI::Initialize` | UIParent; Whether or not script profiling is enabled; Whether taint logging is enabled; scriptProfile |
| 004b81d0 | `CBLPFile::Open` | Error loading texure file "%s": unsupported image ; TextureLoadImage() blocking load: %s.\n; dataFormat; height |
| 00972210 | `CSimpleScrollFrame_SetScrollChild` | %s:SetScrollChild(): Couldn't find 'this' in child; %s:SetScrollChild(): Couldn't find frame named '%s; %s:SetScrollChild(): Would create a loop adding ch; %s:SetScrollChild(): Wrong child object type, expe |
| 0096c9e0 | `CSimpleHTML::ParseP` | .?AUCONTENTNODE@@; .?AVCSimpleTexture@@; height; width |
| 007e2a50 | `ReceiveServerTime` | (High bit set); Malformed message recieved: Id = %d, Len = %d, Rea; Server game time is %s; Server last tick is 0x%08x %s |
| 0078df90 | `CWorldParam::BSPCacheCallback` | BSP node cache already disabled.; Disabling BSP node cache.; Enabling BSP node cache (already enabled, so clear; Enabling BSP node cache (first time - starting up) |

## Largest whoa functions with no reference link

Either the port added behaviour the reference does not have, or the link is simply unknown: tag it with `// ref: FUN_xxxxxxxx` once found.

| whoa | lib | code bytes | file |
|---|---|---:|---|
| `CGxDevice::IRsInit` | gx | 9227 | src/gx/CGxDevice.cpp |
| `LoadWmoInstance` | world | 8306 | src/world/Terrain.cpp |
| `CFF_Parse_CharStrings` | lib/freetype-2.0 | 7964 |  |
| `ParticleFxRender` | world | 7364 | src/world/ParticleFx.cpp |
| `doProlog` | lib/expat-2.0 | 7308 | lib/common/vendor/expat-2.0.1/lib/xmlparse.c |
| `luaV_execute` | lib/lua-5.1 | 6660 |  |
| `inflate` | lib/zlib-1.2 | 5368 |  |
| `CM2Model::AnimateMT` | model | 5240 | src/model/CM2Model.cpp |
| `CWorld::UpdateOutdoorLight` | world | 4843 | src/world/CWorld.cpp |
| `std::_Matcher3<wchar_t,std::regex_traits<wchar_t>,wchar_t const *,void>::_Match_pat` | glue | 4820 |  |
| `SHA1_Transform` | lib/common | 4679 | lib/common/common/SHA1.cpp |
| `ParseLegacyLiquid` | world | 4522 | src/world/Terrain.cpp |
| `M2Init` | model | 4502 | src/model/M2Init.cpp |
| `T1_Decoder_Parse_Charstrings` | lib/freetype-2.0 | 4304 |  |
| `ParseChunk` | world | 4052 | src/world/Terrain.cpp |
| `doContent` | lib/expat-2.0 | 3900 | lib/common/vendor/expat-2.0.1/lib/xmlparse.c |
| `LzmaDec_DecodeReal` | lib/stormlib-9.31 | 3776 | lib/squall/vendor/stormlib-9.31/src/lzma/c/lzmadec.c |
| `CM2Model::InitializeLoaded` | model | 3689 | src/model/CM2Model.cpp |
| `LoadTile` | world | 3528 | src/world/Terrain.cpp |
| `Rebuild` | object | 3317 | src/object/client/SpellBook.cpp |
| `ParticleFxUpdateModel` | world | 3252 | src/world/ParticleFx.cpp |
| `ChrRacesRec::Read` | db | 2957 | src/db/rec/ChrRacesRec.cpp |
| `RenderWmos` | world | 2898 | src/world/Terrain.cpp |
| `MapRec::Read` | db | 2780 | src/db/rec/MapRec.cpp |
| `TerrainUpdateView` | world | 2685 | src/world/Terrain.cpp |
| `ParseLiquid` | world | 2616 | src/world/Terrain.cpp |
| `SFileOpenArchive` | lib/stormlib-9.31 | 2611 | lib/squall/vendor/stormlib-9.31/src/SFileOpenArchive.cpp |
| `AchievementRec::Read` | db | 2604 | src/db/rec/AchievementRec.cpp |
| `storeAtts` | lib/expat-2.0 | 2588 | lib/common/vendor/expat-2.0.1/lib/xmlparse.c |
| `ChrClassesRec::Read` | db | 2561 | src/db/rec/ChrClassesRec.cpp |
| `luaK_posfix` | lib/lua-5.1 | 2528 |  |
| `CGxString::CreateGeometry` | gx | 2492 | src/gx/font/CGxString.cpp |
| `CBackdropGenerator::SetOutput` | ui | 2401 | src/ui/CBackdropGenerator.cpp |
| `normal_prologTok` | lib/expat-2.0 | 2360 | lib/common/vendor/expat-2.0.1/lib/xmltok_impl.c |
| `CM2Scene::Animate` | model | 2337 | src/model/CM2Scene.cpp |
| `TEXTURECACHE::PasteGlyphOutlinedAA` | gx | 2309 | src/gx/font/CGxFont.cpp |
| `BuildSkyDome` | world | 2303 | src/world/Terrain.cpp |
| `FreeTile` | world | 2259 | src/world/Terrain.cpp |
| `TT_CharMap_Load` | lib/freetype-2.0 | 2236 |  |
| `WriteCmpData` | lib/stormlib-9.31 | 2231 | lib/squall/vendor/stormlib-9.31/src/pklib/implode.c |

## Linked ports with the lowest call-order fidelity

The port exists but does not make the calls the reference makes, in the order it makes them. Either the port guessed, or its callees are not yet linked (then `--show` lists them as bare addresses). Non-stub, largest first.

| addr | whoa | call order | ref calls | whoa calls | ref branches | whoa branches | consts | size |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 0051d9b0 | `CGGameUI::RegisterGameCVars` | 97% | 192 | 206 | 4 | 0 | 40% | 6776 |
| 0087c710 | `SESound::Init` | 11% | 230 | 41 | 83 | 5 | 2% | 6078 |
| 006e2e90 | `InventoryChangeFailureHandler` | 7% | 214 | 14 | 104 | 8 | 2% | 4848 |
| 004dab40 | `CGlueMgr::PollAccountLogin` | 7% | 148 | 30 | 90 | 15 | 10% | 3696 |
| 00526530 | `ReceiveWeather` | 3% | 115 | 7 | 40 | 3 | 0% | 2495 |
| 004d1600 | `SI2::RegisterUserCVars` | 28% | 79 | 22 | 25 | 0 | 0% | 2232 |
| 0079e7c0 | `CMap::MapMemInitialize` | 58% | 45 | 26 | 21 | 0 | 16% | 2068 |
| 004932c0 | `CSimpleFrame::LoadXML` | 67% | 69 | 78 | 68 | 36 | 12% | 1858 |
| 004c6a40 | `SI2::PlaySoundKit` | 10% | 48 | 28 | 74 | 24 | 17% | 1787 |
| 0096e9c0 | `StringToClickAction` | 9% | 64 | 6 | 66 | 7 | 0% | 1741 |
| 00405dd0 | `Sub405DD0` | 2% | 50 | 2 | 59 | 0 | 0% | 1706 |
| 007e18c0 | `ValidateNameInitialize` | 0% | 10 | 4 | 90 | 4 | 0% | 1475 |
| 004873e0 | `CSimpleFontString::LoadXML` | 70% | 63 | 68 | 55 | 33 | 0% | 1441 |
| 0087b5f0 | `SEDiskSound::CompleteNonBlockingLoad` | 9% | 33 | 9 | 37 | 5 | 0% | 1395 |
| 00631000 | `CGTooltip_SetAction` | 4% | 51 | 7 | 43 | 2 | 0% | 1377 |
| 007e4480 | `BlobShadowsBegin` | 21% | 33 | 17 | 10 | 2 | 0% | 1370 |
| 0078e400 | `CWorldParam::Initialize` | 84% | 37 | 31 | 4 | 0 | 0% | 1354 |
| 0087ee60 | `SESound::LoadDiskSound` | 76% | 29 | 36 | 35 | 19 | 0% | 1303 |
| 0069ed50 | `CGxDeviceGLL::PatchVertexShader` | 50% | 10 | 9 | 66 | ? | ? | 1299 |
| 0052a980 | `CGGameUI::Initialize` | 14% | 59 | 30 | 23 | 6 | 5% | 1267 |
| 004f1a20 | `CCharacterComponent::Initialize` | 20% | 10 | 10 | 10 | 5 | 29% | 1189 |
| 00479860 | `fallbackSort` | 10% | 10 | 10 | 45 | 34 | 26% | 1182 |
| 0096fed0 | `CSimpleButton::LoadXML` | 50% | 46 | 50 | 34 | 23 | 0% | 1174 |
| 00497070 | `CSimpleFont::LoadXML` | 75% | 36 | 50 | 50 | 23 | 0% | 1155 |
| 00401b60 | `ClientRegisterConsoleCommands` | 33% | 33 | 12 | 3 | 1 | 11% | 1126 |
| 0060a630 | `Script_GetGUIDFromString` | 14% | 44 | 13 | 55 | 12 | 8% | 1122 |
| 0060abf0 | `Script_GetGUIDFromToken` | 71% | 42 | 37 | 34 | 27 | 20% | 1121 |
| 00813ee0 | `FrameXML_ProcessFile` | 76% | 42 | 47 | 35 | 24 | 19% | 1104 |
| 0040a270 | `Paint` | 11% | 27 | 35 | 19 | 12 | 0% | 1055 |
| 004f8ea0 | `CGWorldFrame::OnWorldRender` | 2% | 59 | 84 | 14 | 34 | 0% | 1016 |
| 004debc0 | `Script_GetRealmInfo` | 15% | 53 | 58 | 24 | 17 | 6% | 990 |
| 00967290 | `CSimpleEditBox::LoadXML` | 81% | 36 | 42 | 42 | 20 | 0% | 989 |
| 008ce200 | `FindPatchPrefix_SC2_ArchiveName` | 0% | 42 | 5 | 11 | 10 | 2% | 951 |
| 004da5f0 | `CGlueMgr::Resume` | 13% | 47 | 38 | 15 | 5 | 8% | 909 |
| 004d9bd0 | `CGlueMgr::EnterWorld` | 55% | 38 | 38 | 23 | 15 | 29% | 895 |
| 0048fef0 | `CSimpleFrame::LoadXML_Scripts` | 62% | 24 | 41 | 31 | 18 | 12% | 889 |
| 004067f0 | `InitializeGlobal` | 17% | 52 | 19 | 12 | 1 | 10% | 887 |
| 0076a220 | `CVGxResolutionCallback` | 30% | 20 | 29 | 25 | 12 | 40% | 856 |
| 0061eb40 | `CGTooltip_SetOwner` | 30% | 33 | 14 | 25 | 4 | 0% | 836 |
| 008bfe80 | `EffectGlow::EffectGlow` | 4% | 28 | 1 | 19 | 0 | 0% | 834 |

## Runtime: last call trace (tools/recomp/calltrace.py + tracecompare.py)

Traced 2026-09-18 10:28: 3 frames on each client, frame-level call order agreement 58%, 18 functions verified (same per-frame count), 25 links contradicted (table below).

| reference calls every frame, whoa never (hits) | whoa calls, reference never (hits) |
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

Per-frame count mismatches (ref \| whoa), largest first:

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

| addr | whoa | link evidence | why | ref per frame | whoa per frame |
|---|---|---|---|---|---|
| 00422140 | `SFile::IsStreamingTrial` | callorder | one side every frame, the other never | [7, 4, 3] | [0, 0, 0] |
| 00482aa0 | `FrameScript_Object::GetDisplayName` | unlinked | one side every frame, the other never | [0, 0, 0] | [1, 1, 1] |
| 00483910 | `CSimpleFontString::SetText` | callorder | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 00488540 | `CSimpleButton::Enable` | callorder | one side every frame, the other never | [5, 5, 5] | [0, 0, 0] |
| 0048d730 | `CSimpleFontString_GetText` | table | one side every frame, the other never | [0, 0, 0] | [7, 7, 7] |
| 0048d780 | `CSimpleFontString_SetText` | table | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 0049d3b0 | `CScriptRegion_GetWidth` | table | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 0049d550 | `CScriptRegion_GetHeight` | table | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 0049fe30 | `CSimpleFrame_IsVisible` | table | one side every frame, the other never | [2, 2, 2] | [0, 0, 0] |
| 004c6a40 | `SI2::PlaySoundKit` | string | one side every frame, the other never | [6, 4, 3] | [0, 0, 0] |
| 004dab40 | `CGlueMgr::PollAccountLogin` | string | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 0060abf0 | `Script_GetGUIDFromToken` | annotated | one side every frame, the other never | [2, 2, 2] | [0, 0, 0] |
| 0060eb60 | `Script_UnitHealth` | table | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 0060ed40 | `Script_UnitMana` | table | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 00767460 | `CVar::LookupRegistered` | callorder | one side every frame, the other never | [2, 2, 2] | [0, 0, 0] |
| 0076e720 | `SStrChrR` | callorder | one side every frame, the other never | [6, 3, 2] | [0, 0, 0] |
| 0076f010 | `ISStrVPrintf` | callgraph | one side every frame, the other never | [13, 7, 5] | [0, 0, 0] |
| 007e4480 | `BlobShadowsBegin` | override | one side every frame, the other never | [0, 0, 0] | [1, 1, 1] |
| 008154e0 | `StringToBOOL` | callorder | one side every frame, the other never | [2, 2, 2] | [0, 0, 0] |
| 00819210 | `FrameScript_Execute` | callorder | one side every frame, the other never | [0, 0, 0] | [20, 20, 20] |
| 0084f9f0 | `luaL_checklstring` | callorder | one side every frame, the other never | [1, 1, 1] | [0, 0, 0] |
| 00856ea0 | `luaV_tostring` | override | one side every frame, the other never | [7, 7, 7] | [0, 0, 0] |
| 0085b950 | `luaC_step` | override | one side every frame, the other never | [2, 2, 2] | [0, 0, 0] |
| 0087b5f0 | `SEDiskSound::CompleteNonBlockingLoad` | string | one side every frame, the other never | [6, 3, 2] | [0, 0, 0] |
| 0087ee60 | `SESound::LoadDiskSound` | string | one side every frame, the other never | [6, 3, 2] | [0, 0, 0] |

## Iterations

| run | linked | faithful | stub | spine linked | lua bindings | lua stubs |
|---|---:|---:|---:|---:|---:|---:|
| 2026-09-18 17:02 | 1475 (5.4%) | 225 (0.8%) | 453 | 155/5530 | 936/2512 | 448 |
| 2026-09-18 17:06 | 1482 (5.5%) | 228 (0.8%) | 454 | 162/5530 | 936/2512 | 448 |
| 2026-09-18 17:20 | 1490 (5.5%) | 229 (0.8%) | 453 | 167/5530 | 936/2512 | 448 |
| 2026-09-18 17:25 | 1489 (5.5%) | 229 (0.8%) | 453 | 167/5530 | 936/2512 | 448 |
| 2026-09-18 17:32 | 1489 (5.5%) | 230 (0.8%) | 453 | 167/5530 | 936/2512 | 448 |
| 2026-09-18 17:36 | 1498 (5.5%) | 238 (0.9%) | 453 | 168/5530 | 936/2512 | 448 |
| 2026-09-18 17:42 | 1498 (5.5%) | 238 (0.9%) | 453 | 168/5530 | 936/2512 | 448 |
| 2026-09-18 17:43 | 1498 (5.5%) | 250 (0.9%) | 453 | 168/5530 | 936/2512 | 448 |
| 2026-09-18 17:49 | 1499 (5.5%) | 243 (0.9%) | 451 | 168/5530 | 936/2512 | 446 |
| 2026-09-18 17:54 | 1510 (5.6%) | 246 (0.9%) | 451 | 172/5530 | 936/2512 | 446 |
| 2026-09-18 18:09 | 1513 (5.6%) | 252 (0.9%) | 451 | 171/5530 | 936/2512 | 446 |
| 2026-09-18 18:13 | 1514 (5.6%) | 253 (0.9%) | 451 | 172/5530 | 936/2512 | 446 |
| 2026-09-18 18:17 | 1543 (5.7%) | 255 (0.9%) | 455 | 173/5530 | 936/2512 | 446 |
| 2026-09-18 18:18 | 1543 (5.7%) | 255 (0.9%) | 455 | 173/5530 | 936/2512 | 446 |
| 2026-09-18 18:20 | 1611 (5.9%) | 259 (1.0%) | 457 | 173/5530 | 936/2512 | 446 |
| 2026-09-18 21:58 | 1612 (5.9%) | 274 (1.0%) | 457 | 173/5530 | 936/2512 | 446 |
| 2026-09-18 22:12 | 1620 (6.0%) | 295 (1.1%) | 463 | 174/5530 | 936/2512 | 446 |
| 2026-09-18 22:25 | 1636 (6.0%) | 316 (1.2%) | 463 | 176/5530 | 936/2512 | 446 |
| 2026-09-18 22:28 | 1640 (6.0%) | 319 (1.2%) | 463 | 178/5530 | 936/2512 | 446 |
| 2026-09-18 22:35 | 1647 (6.1%) | 324 (1.2%) | 463 | 179/5530 | 936/2512 | 446 |
| 2026-09-18 22:43 | 1652 (6.1%) | 326 (1.2%) | 463 | 187/5530 | 936/2512 | 446 |
| 2026-09-18 22:44 | 1654 (6.1%) | 327 (1.2%) | 463 | 189/5530 | 936/2512 | 446 |
| 2026-09-18 22:59 | 1647 (6.1%) | 320 (1.2%) | 463 | 186/5530 | 936/2512 | 446 |
| 2026-09-18 23:01 | 1657 (6.1%) | 329 (1.2%) | 463 | 190/5531 | 936/2512 | 446 |
| 2026-09-18 23:05 | 1658 (6.1%) | 329 (1.2%) | 463 | 191/5530 | 936/2512 | 446 |

## How to move a row

1. Pick the top unmapped spine function. `python tools/recomp/recomp.py --show <addr>` prints its callers, callees, strings and the closest whoa candidates; `C:\Users\tyler\tools\decomp.sh <out> <addr>` decompiles it.
2. Port it (or find the existing port) and put `// ref: FUN_<addr>` above the whoa definition.
3. When a run shows it behaving like the reference, add `{"<addr>": {"whoa": "<name>", "status": "verified", "note": "..."}}` to overrides.json.
4. Re-run the tool; the totals line shows the delta against the previous run.
