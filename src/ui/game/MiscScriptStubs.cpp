#include "ui/game/MiscScriptStubs.hpp"

#include "ui/FrameScript.hpp"
#include "ui/Types.hpp"
#include "util/Lua.hpp"

#include <cstdio>

// Interface ACTIONS the reference performs and frozen cannot yet: closing a frame it has no model
// for, selling to a merchant it has no connection to, sorting an auction list that does not
// exist. Unlike the predicates in MiscScript.cpp, where returning nil IS the reference's own
// answer for a client in this state, doing nothing here is NOT what the reference does. These
// are stubs, and each says so the first time it is called, like every other stub here.
//
// Registered anyway because the alternative is worse: a missing global is a hard Lua error that
// takes down the rest of the script that touched it, so the interface loses far more than the
// one action.

// NOT stubbed here, deliberately: names containing an underscore. In 3.3.5a the client's own Lua API
// is PascalCase without underscores; an underscore means the function is defined by an interface
// SCRIPT (BackpackTokenFrame_Update) or by a loadable addon (Blizzard_CombatLog_*). Registering a C
// function for those would hide a different gap entirely -- that frozen is not loading those scripts
// or addons -- behind a name that looks present. 130 such names were removed from this file after
// they were noticed; they belong in the interface-loading work, not here.

namespace {

#define WHOA_LUA_STUB(name)                                                           \
    int32_t Script_Stub_##name(lua_State* L) {                                        \
        static bool reported = false;                                                 \
                                                                                      \
        if (!reported) {                                                              \
            reported = true;                                                          \
            fprintf(stderr, "Function not yet implemented: %s (action stub)\n", #name); \
        }                                                                             \
                                                                                      \
        return 0;                                                                     \
    }

WHOA_LUA_STUB(AcceptDuel)
WHOA_LUA_STUB(AcceptProposal)
WHOA_LUA_STUB(AcceptQuest)
WHOA_LUA_STUB(AcceptSkillUps)
WHOA_LUA_STUB(AcceptSockets)
WHOA_LUA_STUB(AddChatWindowChannel)
WHOA_LUA_STUB(AddChatWindowMessages)
WHOA_LUA_STUB(AddFriend)
WHOA_LUA_STUB(AddIgnore)
WHOA_LUA_STUB(AddMute)
WHOA_LUA_STUB(AddOrDelIgnore)
WHOA_LUA_STUB(AddOrDelMute)
WHOA_LUA_STUB(AddOrRemoveFriend)
WHOA_LUA_STUB(AddPreviewTalentPoints)
WHOA_LUA_STUB(AddQuestWatch)
WHOA_LUA_STUB(AddSkillUp)
WHOA_LUA_STUB(AddTrackedAchievement)
WHOA_LUA_STUB(AscendStop)
WHOA_LUA_STUB(BuyGuildBankTab)
WHOA_LUA_STUB(BuyGuildCharter)
WHOA_LUA_STUB(BuyMerchantItem)
WHOA_LUA_STUB(BuyPetition)
WHOA_LUA_STUB(BuySkillTier)
WHOA_LUA_STUB(BuyStableSlot)
WHOA_LUA_STUB(BuyTrainerService)
WHOA_LUA_STUB(BuybackItem)
WHOA_LUA_STUB(ClearAchievementComparisonUnit)
WHOA_LUA_STUB(ClearAllLFGDungeons)
WHOA_LUA_STUB(ClearChannelWatch)
WHOA_LUA_STUB(ClearLFGDungeon)
WHOA_LUA_STUB(ClearSendMail)
WHOA_LUA_STUB(ClearTutorials)
WHOA_LUA_STUB(CloseArenaTeamRoster)
WHOA_LUA_STUB(CloseAuctionHouse)
WHOA_LUA_STUB(CloseBankFrame)
WHOA_LUA_STUB(CloseGossip)
WHOA_LUA_STUB(CloseGuildBankFrame)
WHOA_LUA_STUB(CloseGuildRegistrar)
WHOA_LUA_STUB(CloseGuildRoster)
WHOA_LUA_STUB(CloseItemText)
WHOA_LUA_STUB(CloseLoot)
WHOA_LUA_STUB(CloseMail)
WHOA_LUA_STUB(CloseMerchant)
WHOA_LUA_STUB(ClosePetStables)
WHOA_LUA_STUB(ClosePetition)
WHOA_LUA_STUB(ClosePetitionVendor)
WHOA_LUA_STUB(CloseQuest)
WHOA_LUA_STUB(CloseSocketInfo)
WHOA_LUA_STUB(CloseTabardCreation)
WHOA_LUA_STUB(CloseTaxiMap)
WHOA_LUA_STUB(CloseTradeSkill)
WHOA_LUA_STUB(CloseTrainer)
WHOA_LUA_STUB(ConfirmAcceptQuest)
WHOA_LUA_STUB(ConfirmLootRoll)
WHOA_LUA_STUB(ConfirmLootSlot)
WHOA_LUA_STUB(DeclineQuest)
WHOA_LUA_STUB(DescendStop)
WHOA_LUA_STUB(JumpOrAscendStart)
WHOA_LUA_STUB(MoveAndSteerStart)
WHOA_LUA_STUB(MoveAndSteerStop)
WHOA_LUA_STUB(MoveBackwardStart)
WHOA_LUA_STUB(MoveBackwardStop)
WHOA_LUA_STUB(MoveForwardStart)
WHOA_LUA_STUB(MoveForwardStop)
WHOA_LUA_STUB(MoveViewDownStart)
WHOA_LUA_STUB(MoveViewDownStop)
WHOA_LUA_STUB(MoveViewInStart)
WHOA_LUA_STUB(MoveViewInStop)
WHOA_LUA_STUB(MoveViewLeftStart)
WHOA_LUA_STUB(MoveViewLeftStop)
WHOA_LUA_STUB(MoveViewOutStart)
WHOA_LUA_STUB(MoveViewOutStop)
WHOA_LUA_STUB(MoveViewRightStart)
WHOA_LUA_STUB(MoveViewRightStop)
WHOA_LUA_STUB(MoveViewUpStart)
WHOA_LUA_STUB(MoveViewUpStop)
WHOA_LUA_STUB(PickupCompanion)
WHOA_LUA_STUB(PickupContainerItem)
WHOA_LUA_STUB(PickupEquipmentSet)
WHOA_LUA_STUB(PickupEquipmentSetByName)
WHOA_LUA_STUB(PickupGuildBankItem)
WHOA_LUA_STUB(PickupGuildBankMoney)
WHOA_LUA_STUB(PickupMacro)
WHOA_LUA_STUB(PickupMerchantItem)
WHOA_LUA_STUB(PickupPetAction)
WHOA_LUA_STUB(PickupSpell)
WHOA_LUA_STUB(PickupStablePet)
WHOA_LUA_STUB(QueryAuctionItems)
WHOA_LUA_STUB(QueryGuildBankLog)
WHOA_LUA_STUB(QueryGuildBankTab)
WHOA_LUA_STUB(QueryGuildBankText)
WHOA_LUA_STUB(QueryGuildEventLog)
WHOA_LUA_STUB(QueryQuestsCompleted)
WHOA_LUA_STUB(RefreshLFGList)
WHOA_LUA_STUB(RemoveChatWindowChannel)
WHOA_LUA_STUB(RemoveChatWindowMessages)
WHOA_LUA_STUB(RemoveFriend)
WHOA_LUA_STUB(RemoveGlyphFromSocket)
WHOA_LUA_STUB(RemoveQuestWatch)
WHOA_LUA_STUB(RemoveSkillUp)
WHOA_LUA_STUB(RemoveTrackedAchievement)
WHOA_LUA_STUB(RequestLFDPartyLockInfo)
WHOA_LUA_STUB(RequestLFDPlayerLockInfo)
WHOA_LUA_STUB(ResetChatColors)
WHOA_LUA_STUB(ResetChatWindows)
WHOA_LUA_STUB(ResetGroupPreviewTalentPoints)
WHOA_LUA_STUB(ResetPreviewTalentPoints)
WHOA_LUA_STUB(ResetTutorials)
WHOA_LUA_STUB(ResetView)
WHOA_LUA_STUB(SendAddonMessage)
WHOA_LUA_STUB(SendChatMessage)
WHOA_LUA_STUB(SendMail)
WHOA_LUA_STUB(SendSystemMessage)
WHOA_LUA_STUB(SendWho)
WHOA_LUA_STUB(SetAchievementComparisonUnit)
WHOA_LUA_STUB(SetActiveTalentGroup)
WHOA_LUA_STUB(SetActiveVoiceChannel)
WHOA_LUA_STUB(SetActiveVoiceChannelBySessionID)
WHOA_LUA_STUB(SetArenaTeamRosterSelection)
WHOA_LUA_STUB(SetArenaTeamRosterShowOffline)
WHOA_LUA_STUB(SetAuctionsTabShowing)
WHOA_LUA_STUB(SetChannelPassword)
WHOA_LUA_STUB(SetChatColorNameByClass)
WHOA_LUA_STUB(SetChatWindowAlpha)
WHOA_LUA_STUB(SetChatWindowColor)
WHOA_LUA_STUB(SetChatWindowDocked)
WHOA_LUA_STUB(SetChatWindowLocked)
WHOA_LUA_STUB(SetChatWindowName)
WHOA_LUA_STUB(SetChatWindowSavedDimensions)
WHOA_LUA_STUB(SetChatWindowSavedPosition)
WHOA_LUA_STUB(SetChatWindowShown)
WHOA_LUA_STUB(SetChatWindowSize)
WHOA_LUA_STUB(SetChatWindowUninteractable)
WHOA_LUA_STUB(SetCurrencyBackpack)
WHOA_LUA_STUB(SetCurrencyUnused)
WHOA_LUA_STUB(SetCurrentGuildBankTab)
WHOA_LUA_STUB(SetDungeonMapLevel)
WHOA_LUA_STUB(SetFactionActive)
WHOA_LUA_STUB(SetFactionInactive)
WHOA_LUA_STUB(SetFriendNotes)
WHOA_LUA_STUB(SetGuildBankTabInfo)
WHOA_LUA_STUB(SetGuildBankTabPermissions)
WHOA_LUA_STUB(SetGuildBankTabWithdraw)
WHOA_LUA_STUB(SetGuildBankText)
WHOA_LUA_STUB(SetGuildBankWithdrawLimit)
WHOA_LUA_STUB(SetGuildInfoText)
WHOA_LUA_STUB(SetGuildRosterShowOffline)
WHOA_LUA_STUB(SetLFGBootVote)
WHOA_LUA_STUB(SetLFGComment)
WHOA_LUA_STUB(SetLFGDungeon)
WHOA_LUA_STUB(SetLFGDungeonEnabled)
WHOA_LUA_STUB(SetLFGHeaderCollapsed)
WHOA_LUA_STUB(SetLFGRoles)
WHOA_LUA_STUB(SetLootPortrait)
WHOA_LUA_STUB(SetMacroItem)
WHOA_LUA_STUB(SetMacroSpell)
WHOA_LUA_STUB(SetMapByID)
WHOA_LUA_STUB(SetMapZoom)
WHOA_LUA_STUB(SetMouselookOverrideBinding)
WHOA_LUA_STUB(SetNextBarberShopStyle)
WHOA_LUA_STUB(SetPOIIconOverlapDistance)
WHOA_LUA_STUB(SetPOIIconOverlapPushDistance)
WHOA_LUA_STUB(SetPetStablePaperdoll)
WHOA_LUA_STUB(SetSavedInstanceExtend)
WHOA_LUA_STUB(SetSelectedAuctionItem)
WHOA_LUA_STUB(SetSelectedDisplayChannel)
WHOA_LUA_STUB(SetSelectedFaction)
WHOA_LUA_STUB(SetSelectedFriend)
WHOA_LUA_STUB(SetSelectedIgnore)
WHOA_LUA_STUB(SetSelectedMute)
WHOA_LUA_STUB(SetSendMailCOD)
WHOA_LUA_STUB(SetSendMailMoney)
WHOA_LUA_STUB(SetSendMailShowing)
WHOA_LUA_STUB(SetTaxiMap)
WHOA_LUA_STUB(SetTradeSkillInvSlotFilter)
WHOA_LUA_STUB(SetTradeSkillItemLevelFilter)
WHOA_LUA_STUB(SetTradeSkillItemNameFilter)
WHOA_LUA_STUB(SetTradeSkillSubClassFilter)
WHOA_LUA_STUB(SetTrainerServiceTypeFilter)
WHOA_LUA_STUB(SetTrainerSkillLineFilter)
WHOA_LUA_STUB(SetView)
WHOA_LUA_STUB(SetWatchedFactionIndex)
WHOA_LUA_STUB(SortArenaTeamRoster)
WHOA_LUA_STUB(SortAuctionApplySort)
WHOA_LUA_STUB(SortAuctionClearSort)
WHOA_LUA_STUB(SortAuctionItems)
WHOA_LUA_STUB(SortAuctionSetSort)
WHOA_LUA_STUB(SortGuildRoster)
WHOA_LUA_STUB(SortQuestWatches)
WHOA_LUA_STUB(SortWho)
WHOA_LUA_STUB(StartAuction)
WHOA_LUA_STUB(StartDuel)
WHOA_LUA_STUB(StopMacro)
WHOA_LUA_STUB(StopTradeSkillRepeat)
WHOA_LUA_STUB(StopwatchCloseButton_OnClick)
WHOA_LUA_STUB(StopwatchFrame_OnDragStart)
WHOA_LUA_STUB(StopwatchFrame_OnDragStop)
WHOA_LUA_STUB(StopwatchFrame_OnEvent)
WHOA_LUA_STUB(StopwatchFrame_OnHide)
WHOA_LUA_STUB(StopwatchFrame_OnLoad)
WHOA_LUA_STUB(StopwatchFrame_OnMouseDown)
WHOA_LUA_STUB(StopwatchFrame_OnMouseUp)
WHOA_LUA_STUB(StopwatchFrame_OnShow)
WHOA_LUA_STUB(StopwatchFrame_OnUpdate)
WHOA_LUA_STUB(StopwatchPlayPauseButton_OnClick)
WHOA_LUA_STUB(StopwatchResetButton_OnClick)
WHOA_LUA_STUB(StopwatchTicker_OnUpdate)
WHOA_LUA_STUB(StopwatchTicker_Update)
WHOA_LUA_STUB(Stopwatch_Clear)
WHOA_LUA_STUB(Stopwatch_FinishCountdown)
int32_t Script_Stub_Stopwatch_IsPlaying(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
WHOA_LUA_STUB(Stopwatch_Pause)
WHOA_LUA_STUB(Stopwatch_Play)
WHOA_LUA_STUB(Stopwatch_StartCountdown)
WHOA_LUA_STUB(Stopwatch_Toggle)
WHOA_LUA_STUB(ToggleAutoRun)
WHOA_LUA_STUB(TogglePetAutocast)
WHOA_LUA_STUB(ToggleRun)
WHOA_LUA_STUB(ToggleSpellAutocast)
WHOA_LUA_STUB(TurnInArenaPetition)
WHOA_LUA_STUB(TurnInGuildCharter)
WHOA_LUA_STUB(TurnInPetition)
WHOA_LUA_STUB(TurnLeftStart)
WHOA_LUA_STUB(TurnLeftStop)
WHOA_LUA_STUB(TurnOrActionStart)
WHOA_LUA_STUB(TurnOrActionStop)
WHOA_LUA_STUB(TurnRightStart)
WHOA_LUA_STUB(TurnRightStop)
WHOA_LUA_STUB(UseContainerItem)
WHOA_LUA_STUB(UseEquipmentSet)
WHOA_LUA_STUB(UseQuestLogSpecialItem)

WHOA_LUA_STUB(AbandonQuest)
WHOA_LUA_STUB(AbandonSkill)
WHOA_LUA_STUB(ApplyBarberShopStyle)
WHOA_LUA_STUB(ArenaTeamRoster)
WHOA_LUA_STUB(AutoLootMailItem)
WHOA_LUA_STUB(AutoStoreGuildBankItem)
WHOA_LUA_STUB(BankButtonIDToInvSlotID)
WHOA_LUA_STUB(BarberShopReset)
WHOA_LUA_STUB(CalculateAuctionDeposit)
WHOA_LUA_STUB(CalendarAddEvent)
WHOA_LUA_STUB(CalendarCanAddEvent)
WHOA_LUA_STUB(CalendarCanSendInvite)
WHOA_LUA_STUB(CalendarCloseEvent)
WHOA_LUA_STUB(CalendarContextDeselectEvent)
WHOA_LUA_STUB(CalendarContextEventCanComplain)
WHOA_LUA_STUB(CalendarContextEventCanEdit)
WHOA_LUA_STUB(CalendarContextEventClipboard)
WHOA_LUA_STUB(CalendarContextEventComplain)
WHOA_LUA_STUB(CalendarContextEventCopy)
WHOA_LUA_STUB(CalendarContextEventGetCalendarType)
WHOA_LUA_STUB(CalendarContextEventPaste)
WHOA_LUA_STUB(CalendarContextEventRemove)
WHOA_LUA_STUB(CalendarContextEventSignUp)
WHOA_LUA_STUB(CalendarContextGetEventIndex)
WHOA_LUA_STUB(CalendarContextInviteAvailable)
WHOA_LUA_STUB(CalendarContextInviteDecline)
WHOA_LUA_STUB(CalendarContextInviteIsPending)
WHOA_LUA_STUB(CalendarContextInviteModeratorStatus)
WHOA_LUA_STUB(CalendarContextInviteRemove)
WHOA_LUA_STUB(CalendarContextInviteStatus)
WHOA_LUA_STUB(CalendarContextInviteTentative)
WHOA_LUA_STUB(CalendarContextInviteType)
WHOA_LUA_STUB(CalendarContextSelectEvent)
WHOA_LUA_STUB(CalendarDefaultGuildFilter)
WHOA_LUA_STUB(CalendarEventAvailable)
WHOA_LUA_STUB(CalendarEventCanEdit)
WHOA_LUA_STUB(CalendarEventCanModerate)
WHOA_LUA_STUB(CalendarEventClearAutoApprove)
WHOA_LUA_STUB(CalendarEventClearLocked)
WHOA_LUA_STUB(CalendarEventClearModerator)
WHOA_LUA_STUB(CalendarEventDecline)
WHOA_LUA_STUB(CalendarEventGetCalendarType)
WHOA_LUA_STUB(CalendarEventGetInvite)
WHOA_LUA_STUB(CalendarEventGetInviteResponseTime)
WHOA_LUA_STUB(CalendarEventGetRepeatOptions)
WHOA_LUA_STUB(CalendarEventGetSelectedInvite)
WHOA_LUA_STUB(CalendarEventGetStatusOptions)
WHOA_LUA_STUB(CalendarEventGetTextures)
WHOA_LUA_STUB(CalendarEventGetTypes)
WHOA_LUA_STUB(CalendarEventHasPendingInvite)
WHOA_LUA_STUB(CalendarEventInvite)
WHOA_LUA_STUB(CalendarEventIsModerator)
WHOA_LUA_STUB(CalendarEventRemoveInvite)
WHOA_LUA_STUB(CalendarEventSelectInvite)
WHOA_LUA_STUB(CalendarEventSetAutoApprove)
WHOA_LUA_STUB(CalendarEventSetDate)
WHOA_LUA_STUB(CalendarEventSetDescription)
WHOA_LUA_STUB(CalendarEventSetLocked)
WHOA_LUA_STUB(CalendarEventSetLockoutDate)
WHOA_LUA_STUB(CalendarEventSetLockoutTime)
WHOA_LUA_STUB(CalendarEventSetModerator)
WHOA_LUA_STUB(CalendarEventSetRepeatOption)
WHOA_LUA_STUB(CalendarEventSetSize)
WHOA_LUA_STUB(CalendarEventSetStatus)
WHOA_LUA_STUB(CalendarEventSetTextureID)
WHOA_LUA_STUB(CalendarEventSetTime)
WHOA_LUA_STUB(CalendarEventSetTitle)
WHOA_LUA_STUB(CalendarEventSetType)
WHOA_LUA_STUB(CalendarEventSignUp)
WHOA_LUA_STUB(CalendarEventSortInvites)
WHOA_LUA_STUB(CalendarEventTentative)
WHOA_LUA_STUB(CalendarGetAbsMonth)
WHOA_LUA_STUB(CalendarGetDayEvent)
WHOA_LUA_STUB(CalendarGetDayEventSequenceInfo)
WHOA_LUA_STUB(CalendarGetEventIndex)
WHOA_LUA_STUB(CalendarGetEventInfo)
WHOA_LUA_STUB(CalendarGetFirstPendingInvite)
WHOA_LUA_STUB(CalendarGetHolidayInfo)
WHOA_LUA_STUB(CalendarGetMaxCreateDate)
WHOA_LUA_STUB(CalendarGetMaxDate)
WHOA_LUA_STUB(CalendarGetMinDate)
WHOA_LUA_STUB(CalendarGetMinHistoryDate)
WHOA_LUA_STUB(CalendarGetMonth)
WHOA_LUA_STUB(CalendarGetMonthNames)
WHOA_LUA_STUB(CalendarGetNumDayEvents)
WHOA_LUA_STUB(CalendarGetRaidInfo)
WHOA_LUA_STUB(CalendarGetWeekdayNames)
WHOA_LUA_STUB(CalendarMassInviteArenaTeam)
WHOA_LUA_STUB(CalendarMassInviteGuild)
WHOA_LUA_STUB(CalendarNewEvent)
WHOA_LUA_STUB(CalendarNewGuildAnnouncement)
WHOA_LUA_STUB(CalendarNewGuildEvent)
WHOA_LUA_STUB(CalendarOpenEvent)
WHOA_LUA_STUB(CalendarRemoveEvent)
WHOA_LUA_STUB(CalendarSetAbsMonth)
WHOA_LUA_STUB(CalendarSetMonth)
WHOA_LUA_STUB(CalendarUpdateEvent)
WHOA_LUA_STUB(CallCompanion)
WHOA_LUA_STUB(CameraOrSelectOrMoveStart)
WHOA_LUA_STUB(CameraOrSelectOrMoveStop)
WHOA_LUA_STUB(CameraZoomIn)
WHOA_LUA_STUB(CameraZoomOut)
WHOA_LUA_STUB(CastPetAction)
WHOA_LUA_STUB(CastShapeshiftForm)
WHOA_LUA_STUB(CastSpellByID)
WHOA_LUA_STUB(CastSpellByName)
WHOA_LUA_STUB(ChangeChatColor)
WHOA_LUA_STUB(ChannelSilenceAll)
WHOA_LUA_STUB(ChannelSilenceVoice)
WHOA_LUA_STUB(ChannelUnSilenceAll)
WHOA_LUA_STUB(ChannelUnSilenceVoice)
WHOA_LUA_STUB(CheckInbox)
WHOA_LUA_STUB(ClickAuctionSellItemButton)
WHOA_LUA_STUB(ClickLandmark)
WHOA_LUA_STUB(ClickPetitionButton)
WHOA_LUA_STUB(ClickSendMailItemButton)
WHOA_LUA_STUB(ClickSocketButton)
WHOA_LUA_STUB(ClickStablePet)
WHOA_LUA_STUB(CollapseAllFactionHeaders)
WHOA_LUA_STUB(CollapseChannelHeader)
WHOA_LUA_STUB(CollapseFactionHeader)
WHOA_LUA_STUB(CollapseQuestHeader)
WHOA_LUA_STUB(CollapseSkillHeader)
WHOA_LUA_STUB(CollapseTradeSkillSubClass)
WHOA_LUA_STUB(CollapseTrainerSkillLine)
WHOA_LUA_STUB(CombatLogAddFilter)
WHOA_LUA_STUB(CombatLogAdvanceEntry)
WHOA_LUA_STUB(CombatLogClearEntries)
WHOA_LUA_STUB(CombatLogGetCurrentEntry)
// Not a stub: the count is consumed arithmetically by Blizzard_CombatLog
// (`count = max(1, min(count, COMBATLOG_MESSAGE_LIMIT))`), so answering nothing raised on every
// refresh. No combat log is stored yet, so the count is genuinely zero.
int32_t Script_Stub_CombatLogGetNumEntries(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}
WHOA_LUA_STUB(CombatLogGetRetentionTime)
WHOA_LUA_STUB(CombatLogResetFilter)
WHOA_LUA_STUB(CombatLogSetCurrentEntry)
WHOA_LUA_STUB(CombatLogSetRetentionTime)
WHOA_LUA_STUB(CombatTextSetActiveUnit)
WHOA_LUA_STUB(CommentatorAddPlayer)
WHOA_LUA_STUB(CommentatorEnterInstance)
WHOA_LUA_STUB(CommentatorExitInstance)
WHOA_LUA_STUB(CommentatorFollowPlayer)
WHOA_LUA_STUB(CommentatorGetCamera)
WHOA_LUA_STUB(CommentatorGetCurrentMapID)
WHOA_LUA_STUB(CommentatorGetInstanceInfo)
WHOA_LUA_STUB(CommentatorGetMapInfo)
WHOA_LUA_STUB(CommentatorGetMode)
WHOA_LUA_STUB(CommentatorGetNumMaps)
WHOA_LUA_STUB(CommentatorGetNumPlayers)
WHOA_LUA_STUB(CommentatorGetPlayerInfo)
WHOA_LUA_STUB(CommentatorGetSkirmishMode)
WHOA_LUA_STUB(CommentatorGetSkirmishQueueCount)
WHOA_LUA_STUB(CommentatorGetSkirmishQueuePlayerInfo)
WHOA_LUA_STUB(CommentatorLookatPlayer)
WHOA_LUA_STUB(CommentatorRemovePlayer)
WHOA_LUA_STUB(CommentatorRequestSkirmishMode)
WHOA_LUA_STUB(CommentatorRequestSkirmishQueueData)
WHOA_LUA_STUB(CommentatorSetBattlemaster)
WHOA_LUA_STUB(CommentatorSetCamera)
WHOA_LUA_STUB(CommentatorSetCameraCollision)
WHOA_LUA_STUB(CommentatorSetMapAndInstanceIndex)
WHOA_LUA_STUB(CommentatorSetMode)
WHOA_LUA_STUB(CommentatorSetMoveSpeed)
WHOA_LUA_STUB(CommentatorSetPlayerIndex)
WHOA_LUA_STUB(CommentatorSetSkirmishMatchmakingMode)
WHOA_LUA_STUB(CommentatorSetTargetHeightOffset)
WHOA_LUA_STUB(CommentatorStartInstance)
WHOA_LUA_STUB(CommentatorStartSkirmishMatch)
WHOA_LUA_STUB(CommentatorToggleMode)
WHOA_LUA_STUB(CommentatorUpdateMapInfo)
WHOA_LUA_STUB(CommentatorUpdatePlayerInfo)
WHOA_LUA_STUB(CommentatorZoomIn)
WHOA_LUA_STUB(CommentatorZoomOut)
WHOA_LUA_STUB(ComplainChat)
WHOA_LUA_STUB(ComplainInboxItem)
WHOA_LUA_STUB(CompleteLFGRoleCheck)
WHOA_LUA_STUB(CompleteQuest)
WHOA_LUA_STUB(ContainerRefundItemPurchase)
WHOA_LUA_STUB(CreateMacro)
WHOA_LUA_STUB(CreateMiniWorldMapArrowFrame)
WHOA_LUA_STUB(DelIgnore)
WHOA_LUA_STUB(DelMute)
WHOA_LUA_STUB(DeleteEquipmentSet)
WHOA_LUA_STUB(DeleteInboxItem)
WHOA_LUA_STUB(DeleteMacro)
WHOA_LUA_STUB(DepositGuildBankMoney)
WHOA_LUA_STUB(DetectWowMouse)
WHOA_LUA_STUB(DisableSpellAutocast)
WHOA_LUA_STUB(DismissCompanion)
WHOA_LUA_STUB(DisplayChannelVoiceOff)
WHOA_LUA_STUB(DisplayChannelVoiceOn)
WHOA_LUA_STUB(DoEmote)
WHOA_LUA_STUB(DoTradeSkill)
int32_t Script_Stub_DungeonUsesTerrainMap(lua_State* L) {
    lua_pushboolean(L, 0);

    return 1;
}
WHOA_LUA_STUB(EditMacro)
WHOA_LUA_STUB(EnableSpellAutocast)
WHOA_LUA_STUB(EnumerateServerChannels)
WHOA_LUA_STUB(EquipmentManagerClearIgnoredSlotsForSave)
WHOA_LUA_STUB(EquipmentManagerIgnoreSlotForSave)
WHOA_LUA_STUB(EquipmentManagerIsSlotIgnoredForSave)
WHOA_LUA_STUB(EquipmentManagerUnignoreSlotForSave)
WHOA_LUA_STUB(EquipmentSetContainsLockedItems)
WHOA_LUA_STUB(ExpandAllFactionHeaders)
WHOA_LUA_STUB(ExpandChannelHeader)
WHOA_LUA_STUB(ExpandCurrencyList)
WHOA_LUA_STUB(ExpandFactionHeader)
WHOA_LUA_STUB(ExpandQuestHeader)
WHOA_LUA_STUB(ExpandSkillHeader)
WHOA_LUA_STUB(ExpandTradeSkillSubClass)
WHOA_LUA_STUB(ExpandTrainerSkillLine)
WHOA_LUA_STUB(FactionToggleAtWar)
WHOA_LUA_STUB(FindSpellBookSlotByID)
WHOA_LUA_STUB(FlagTutorial)
WHOA_LUA_STUB(FlipCameraYaw)
WHOA_LUA_STUB(ForceGossip)
WHOA_LUA_STUB(GetAbandonQuestItems)
WHOA_LUA_STUB(GetAbandonQuestName)
WHOA_LUA_STUB(GetAchievementCategory)
WHOA_LUA_STUB(GetAchievementComparisonInfo)
WHOA_LUA_STUB(GetAchievementCriteriaInfo)
WHOA_LUA_STUB(GetAchievementInfo)
WHOA_LUA_STUB(GetAchievementInfoFromCriteria)
WHOA_LUA_STUB(GetAchievementLink)
WHOA_LUA_STUB(GetAchievementNumCriteria)
WHOA_LUA_STUB(GetAchievementNumRewards)
WHOA_LUA_STUB(GetAchievementReward)
WHOA_LUA_STUB(GetActiveLevel)
WHOA_LUA_STUB(GetActiveTitle)
WHOA_LUA_STUB(GetActiveVoiceChannel)
WHOA_LUA_STUB(GetArenaTeamGdfInfo)
WHOA_LUA_STUB(GetArenaTeamRosterInfo)
WHOA_LUA_STUB(GetArenaTeamRosterSelection)
WHOA_LUA_STUB(GetArenaTeamRosterShowOffline)
WHOA_LUA_STUB(GetAuctionHouseDepositRate)
WHOA_LUA_STUB(GetAuctionInvTypes)
WHOA_LUA_STUB(GetAuctionItemClasses)
WHOA_LUA_STUB(GetAuctionItemInfo)
WHOA_LUA_STUB(GetAuctionItemLink)
WHOA_LUA_STUB(GetAuctionItemSubClasses)
WHOA_LUA_STUB(GetAuctionItemTimeLeft)
WHOA_LUA_STUB(GetAuctionSellItemInfo)
WHOA_LUA_STUB(GetAuctionSort)
WHOA_LUA_STUB(GetAutoCompletePresenceID)
WHOA_LUA_STUB(GetAutoCompleteResults)
WHOA_LUA_STUB(GetAvailableLevel)
WHOA_LUA_STUB(GetAvailableQuestInfo)
int32_t Script_Stub_GetAvailableRoles(lua_State* L) {
    // canBeTank, canBeHealer, canBeDPS -- all from the LFG system.
    lua_pushboolean(L, 0);
    lua_pushboolean(L, 0);
    lua_pushboolean(L, 0);

    return 3;
}
WHOA_LUA_STUB(GetAvailableTitle)
WHOA_LUA_STUB(GetBackpackCurrencyInfo)
WHOA_LUA_STUB(GetBankSlotCost)
WHOA_LUA_STUB(GetBarberShopStyleInfo)
WHOA_LUA_STUB(GetBarberShopTotalCost)
WHOA_LUA_STUB(GetBidderAuctionItems)
WHOA_LUA_STUB(GetBuybackItemInfo)
WHOA_LUA_STUB(GetBuybackItemLink)
WHOA_LUA_STUB(GetCategoryInfo)
WHOA_LUA_STUB(GetCategoryList)
WHOA_LUA_STUB(GetCategoryNumAchievements)
WHOA_LUA_STUB(GetChannelList)
WHOA_LUA_STUB(GetChannelName)
WHOA_LUA_STUB(GetChannelRosterInfo)
WHOA_LUA_STUB(GetChatWindowChannels)
WHOA_LUA_STUB(GetChatWindowMessages)
WHOA_LUA_STUB(GetChatWindowSavedDimensions)
WHOA_LUA_STUB(GetChatWindowSavedPosition)
WHOA_LUA_STUB(GetCompanionCooldown)
WHOA_LUA_STUB(GetComparisonAchievementPoints)
WHOA_LUA_STUB(GetComparisonCategoryNumAchievements)
WHOA_LUA_STUB(GetComparisonStatistic)
WHOA_LUA_STUB(GetContainerItemCooldown)
WHOA_LUA_STUB(GetContainerItemGems)
WHOA_LUA_STUB(GetContainerItemPurchaseInfo)
WHOA_LUA_STUB(GetContainerItemPurchaseItem)
WHOA_LUA_STUB(GetContainerItemQuestInfo)
WHOA_LUA_STUB(GetCorpseMapPosition)
WHOA_LUA_STUB(GetCurrencyListInfo)
int32_t Script_Stub_GetCurrencyListSize(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}
WHOA_LUA_STUB(GetCurrentGuildBankTab)
WHOA_LUA_STUB(GetCurrentMapAreaID)
WHOA_LUA_STUB(GetCurrentMapZone)
WHOA_LUA_STUB(GetDailyQuestsCompleted)
WHOA_LUA_STUB(GetDeathReleasePosition)
WHOA_LUA_STUB(GetDebugZoneMap)
WHOA_LUA_STUB(GetDefaultLanguage)
WHOA_LUA_STUB(GetEquipmentSetInfo)
WHOA_LUA_STUB(GetEquipmentSetInfoByName)
WHOA_LUA_STUB(GetEquipmentSetItemIDs)
WHOA_LUA_STUB(GetEquipmentSetLocations)
WHOA_LUA_STUB(GetExistingSocketInfo)
WHOA_LUA_STUB(GetExistingSocketLink)
WHOA_LUA_STUB(GetFactionInfo)
WHOA_LUA_STUB(GetFactionInfoByID)
WHOA_LUA_STUB(GetFirstTradeSkill)
WHOA_LUA_STUB(GetFriendInfo)
WHOA_LUA_STUB(GetGlyphLink)
WHOA_LUA_STUB(GetGlyphSocketInfo)
WHOA_LUA_STUB(GetGossipActiveQuests)
WHOA_LUA_STUB(GetGossipAvailableQuests)
WHOA_LUA_STUB(GetGossipOptions)
WHOA_LUA_STUB(GetGossipText)
WHOA_LUA_STUB(GetGreetingText)
int32_t Script_Stub_GetGroupPreviewTalentPointsSpent(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}
WHOA_LUA_STUB(GetGuildBankItemInfo)
WHOA_LUA_STUB(GetGuildBankItemLink)
WHOA_LUA_STUB(GetGuildBankMoney)
WHOA_LUA_STUB(GetGuildBankMoneyTransaction)
WHOA_LUA_STUB(GetGuildBankTabCost)
WHOA_LUA_STUB(GetGuildBankTabInfo)
WHOA_LUA_STUB(GetGuildBankTabPermissions)
WHOA_LUA_STUB(GetGuildBankText)
WHOA_LUA_STUB(GetGuildBankTransaction)
WHOA_LUA_STUB(GetGuildBankWithdrawLimit)
WHOA_LUA_STUB(GetGuildBankWithdrawMoney)
WHOA_LUA_STUB(GetGuildCharterCost)
WHOA_LUA_STUB(GetGuildEventInfo)
WHOA_LUA_STUB(GetGuildInfoText)
WHOA_LUA_STUB(GetGuildRosterInfo)
WHOA_LUA_STUB(GetGuildRosterLastOnline)
WHOA_LUA_STUB(GetGuildRosterSelection)
WHOA_LUA_STUB(GetGuildRosterShowOffline)
WHOA_LUA_STUB(GetGuildTabardFileNames)
WHOA_LUA_STUB(GetIgnoreName)
WHOA_LUA_STUB(GetInboxHeaderInfo)
WHOA_LUA_STUB(GetInboxInvoiceInfo)
WHOA_LUA_STUB(GetInboxItem)
WHOA_LUA_STUB(GetInboxItemLink)
WHOA_LUA_STUB(GetInboxNumItems)
WHOA_LUA_STUB(GetInboxText)
WHOA_LUA_STUB(GetLFDChoiceCollapseState)
WHOA_LUA_STUB(GetLFDChoiceEnabledState)
WHOA_LUA_STUB(GetLFDChoiceInfo)
WHOA_LUA_STUB(GetLFDChoiceLockedState)
WHOA_LUA_STUB(GetLFDChoiceOrder)
WHOA_LUA_STUB(GetLFDLockInfo)
int32_t Script_Stub_GetLFDLockPlayerCount(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}
WHOA_LUA_STUB(GetLFGBootProposal)
WHOA_LUA_STUB(GetLFGCompletionReward)
WHOA_LUA_STUB(GetLFGCompletionRewardItem)
int32_t Script_Stub_GetLFGDungeonInfo(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}
WHOA_LUA_STUB(GetLFGDungeonRewardInfo)
WHOA_LUA_STUB(GetLFGDungeonRewardLink)
WHOA_LUA_STUB(GetLFGDungeonRewards)
WHOA_LUA_STUB(GetLFGInfoLocal)
WHOA_LUA_STUB(GetLFGProposalEncounter)
WHOA_LUA_STUB(GetLFGProposalMember)
WHOA_LUA_STUB(GetLFGQueueStats)
int32_t Script_Stub_GetLFGRandomCooldownExpiration(lua_State* L) {
    // No LFG, so no cooldown from it.
    lua_pushnumber(L, 0.0);

    return 1;
}
WHOA_LUA_STUB(GetLFGRandomDungeonInfo)
WHOA_LUA_STUB(GetLFGRoleUpdateMember)
WHOA_LUA_STUB(GetLFGRoleUpdateSlot)
WHOA_LUA_STUB(GetLFGTypes)
WHOA_LUA_STUB(GetLFRChoiceOrder)
WHOA_LUA_STUB(GetLanguageByIndex)
WHOA_LUA_STUB(GetLastQueueStatusIndex)
WHOA_LUA_STUB(GetLatestCompletedAchievements)
WHOA_LUA_STUB(GetLatestCompletedComparisonAchievements)
WHOA_LUA_STUB(GetLatestThreeSenders)
WHOA_LUA_STUB(GetLatestUpdatedComparisonStats)
WHOA_LUA_STUB(GetLatestUpdatedStats)
WHOA_LUA_STUB(GetLootRollItemInfo)
WHOA_LUA_STUB(GetLootRollItemLink)
WHOA_LUA_STUB(GetLootRollTimeLeft)
WHOA_LUA_STUB(GetLootSlotInfo)
WHOA_LUA_STUB(GetLootSlotLink)
WHOA_LUA_STUB(GetMacroBody)
WHOA_LUA_STUB(GetMacroIconInfo)
WHOA_LUA_STUB(GetMacroIndexByName)
WHOA_LUA_STUB(GetMacroInfo)
WHOA_LUA_STUB(GetMacroItem)
WHOA_LUA_STUB(GetMacroItemIconInfo)
WHOA_LUA_STUB(GetMacroSpell)
WHOA_LUA_STUB(GetMapContinents)
WHOA_LUA_STUB(GetMapDebugObjectInfo)
WHOA_LUA_STUB(GetMapLandmarkInfo)
WHOA_LUA_STUB(GetMapOverlayInfo)
WHOA_LUA_STUB(GetMapZones)
WHOA_LUA_STUB(GetMaxArenaCurrency)
int32_t Script_Stub_GetMaxDailyQuests(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}
WHOA_LUA_STUB(GetMerchantItemCostInfo)
WHOA_LUA_STUB(GetMerchantItemCostItem)
WHOA_LUA_STUB(GetMerchantItemInfo)
WHOA_LUA_STUB(GetMerchantItemLink)
WHOA_LUA_STUB(GetMerchantItemMaxStack)
WHOA_LUA_STUB(GetMerchantNumItems)
WHOA_LUA_STUB(GetMinigameState)
WHOA_LUA_STUB(GetMinigameType)
WHOA_LUA_STUB(GetMuteName)
WHOA_LUA_STUB(GetMuteStatus)
WHOA_LUA_STUB(GetNewSocketInfo)
WHOA_LUA_STUB(GetNewSocketLink)
WHOA_LUA_STUB(GetNextAchievement)
WHOA_LUA_STUB(GetNextCompleatedTutorial)
WHOA_LUA_STUB(GetNextStableSlotCost)
WHOA_LUA_STUB(GetOwnerAuctionItems)
WHOA_LUA_STUB(GetPackageInfo)
WHOA_LUA_STUB(GetPartyLFGBackfillInfo)
WHOA_LUA_STUB(GetPetActionSlotUsable)
WHOA_LUA_STUB(GetPetActionsUsable)
WHOA_LUA_STUB(GetPetExperience)
int32_t Script_Stub_GetPetFoodTypes(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}
WHOA_LUA_STUB(GetPetHappiness)
WHOA_LUA_STUB(GetPetIcon)
WHOA_LUA_STUB(GetPetTalentTree)
WHOA_LUA_STUB(GetPetTimeRemaining)
WHOA_LUA_STUB(GetPetitionInfo)
WHOA_LUA_STUB(GetPetitionItemInfo)
WHOA_LUA_STUB(GetPetitionNameInfo)
WHOA_LUA_STUB(GetPlayerMapPosition)
WHOA_LUA_STUB(GetPrevCompleatedTutorial)
WHOA_LUA_STUB(GetPreviousAchievement)
WHOA_LUA_STUB(GetQuestBackgroundMaterial)
WHOA_LUA_STUB(GetQuestGreenRange)
WHOA_LUA_STUB(GetQuestIndexForTimer)
WHOA_LUA_STUB(GetQuestIndexForWatch)
WHOA_LUA_STUB(GetQuestItemInfo)
WHOA_LUA_STUB(GetQuestItemLink)
WHOA_LUA_STUB(GetQuestLink)
WHOA_LUA_STUB(GetQuestLogChoiceInfo)
WHOA_LUA_STUB(GetQuestLogCompletionText)
WHOA_LUA_STUB(GetQuestLogGroupNum)
WHOA_LUA_STUB(GetQuestLogItemDrop)
WHOA_LUA_STUB(GetQuestLogItemLink)
WHOA_LUA_STUB(GetQuestLogLeaderBoard)
WHOA_LUA_STUB(GetQuestLogPushable)
WHOA_LUA_STUB(GetQuestLogQuestText)
WHOA_LUA_STUB(GetQuestLogRequiredMoney)
WHOA_LUA_STUB(GetQuestLogRewardArenaPoints)
WHOA_LUA_STUB(GetQuestLogRewardFactionInfo)
WHOA_LUA_STUB(GetQuestLogRewardHonor)
WHOA_LUA_STUB(GetQuestLogRewardInfo)
WHOA_LUA_STUB(GetQuestLogRewardMoney)
WHOA_LUA_STUB(GetQuestLogRewardSpell)
WHOA_LUA_STUB(GetQuestLogRewardTalents)
WHOA_LUA_STUB(GetQuestLogRewardTitle)
WHOA_LUA_STUB(GetQuestLogRewardXP)
WHOA_LUA_STUB(GetQuestLogSpecialItemCooldown)
WHOA_LUA_STUB(GetQuestLogSpecialItemInfo)
WHOA_LUA_STUB(GetQuestLogSpellLink)
WHOA_LUA_STUB(GetQuestLogTimeLeft)
WHOA_LUA_STUB(GetQuestLogTitle)
WHOA_LUA_STUB(GetQuestMoneyToGet)
WHOA_LUA_STUB(GetQuestPOILeaderBoard)
int32_t Script_Stub_GetQuestResetTime(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}
WHOA_LUA_STUB(GetQuestReward)
WHOA_LUA_STUB(GetQuestSortIndex)
WHOA_LUA_STUB(GetQuestSpellLink)
WHOA_LUA_STUB(GetQuestText)
WHOA_LUA_STUB(GetQuestWatchIndex)
WHOA_LUA_STUB(GetQuestWorldMapAreaID)
WHOA_LUA_STUB(GetQuestsCompleted)
WHOA_LUA_STUB(GetRandomDungeonBestChoice)
WHOA_LUA_STUB(GetRewardArenaPoints)
WHOA_LUA_STUB(GetRewardHonor)
WHOA_LUA_STUB(GetRewardMoney)
WHOA_LUA_STUB(GetRewardTalents)
WHOA_LUA_STUB(GetRewardText)
WHOA_LUA_STUB(GetRewardTitle)
WHOA_LUA_STUB(GetRewardXP)
WHOA_LUA_STUB(GetRunningMacro)
WHOA_LUA_STUB(GetRunningMacroButton)
WHOA_LUA_STUB(GetSavedInstanceInfo)
WHOA_LUA_STUB(GetSelectedAuctionItem)
WHOA_LUA_STUB(GetSelectedFaction)
WHOA_LUA_STUB(GetSelectedFriend)
WHOA_LUA_STUB(GetSelectedIgnore)
WHOA_LUA_STUB(GetSelectedMute)
WHOA_LUA_STUB(GetSelectedStablePet)
WHOA_LUA_STUB(GetSelectedStationeryTexture)
WHOA_LUA_STUB(GetSendMailCOD)
WHOA_LUA_STUB(GetSendMailItem)
WHOA_LUA_STUB(GetSendMailItemLink)
WHOA_LUA_STUB(GetSendMailMoney)
WHOA_LUA_STUB(GetShapeshiftForm)
WHOA_LUA_STUB(GetShapeshiftFormCooldown)
WHOA_LUA_STUB(GetShapeshiftFormInfo)
WHOA_LUA_STUB(GetSocketItemBoundTradeable)
WHOA_LUA_STUB(GetSocketItemInfo)
WHOA_LUA_STUB(GetSocketItemRefundable)
WHOA_LUA_STUB(GetSocketTypes)
WHOA_LUA_STUB(GetSpellCount)
int32_t Script_Stub_GetStablePetFoodTypes(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}
WHOA_LUA_STUB(GetStablePetInfo)
WHOA_LUA_STUB(GetStationeryInfo)
WHOA_LUA_STUB(GetStatistic)
WHOA_LUA_STUB(GetStatisticsCategoryList)
WHOA_LUA_STUB(GetSuggestedGroupNum)
WHOA_LUA_STUB(GetTabardInfo)
WHOA_LUA_STUB(GetTalentInfo)
WHOA_LUA_STUB(GetTalentLink)
WHOA_LUA_STUB(GetTalentPrereqs)
WHOA_LUA_STUB(GetTitleText)
WHOA_LUA_STUB(GetTotalAchievementPoints)
WHOA_LUA_STUB(GetTradeSkillCooldown)
WHOA_LUA_STUB(GetTradeSkillDescription)
WHOA_LUA_STUB(GetTradeSkillIcon)
WHOA_LUA_STUB(GetTradeSkillInfo)
WHOA_LUA_STUB(GetTradeSkillInvSlotFilter)
WHOA_LUA_STUB(GetTradeSkillInvSlots)
WHOA_LUA_STUB(GetTradeSkillItemLevelFilter)
WHOA_LUA_STUB(GetTradeSkillItemLink)
WHOA_LUA_STUB(GetTradeSkillItemNameFilter)
WHOA_LUA_STUB(GetTradeSkillLine)
WHOA_LUA_STUB(GetTradeSkillListLink)
WHOA_LUA_STUB(GetTradeSkillNumMade)
WHOA_LUA_STUB(GetTradeSkillNumReagents)
WHOA_LUA_STUB(GetTradeSkillReagentInfo)
WHOA_LUA_STUB(GetTradeSkillReagentItemLink)
WHOA_LUA_STUB(GetTradeSkillRecipeLink)
WHOA_LUA_STUB(GetTradeSkillSelectionIndex)
WHOA_LUA_STUB(GetTradeSkillSubClassFilter)
WHOA_LUA_STUB(GetTradeSkillSubClasses)
WHOA_LUA_STUB(GetTradeSkillTools)
WHOA_LUA_STUB(GetTradeskillRepeatCount)
WHOA_LUA_STUB(GetTrainerGreetingText)
WHOA_LUA_STUB(GetTrainerSelectionIndex)
WHOA_LUA_STUB(GetTrainerServiceAbilityReq)
WHOA_LUA_STUB(GetTrainerServiceCost)
WHOA_LUA_STUB(GetTrainerServiceDescription)
WHOA_LUA_STUB(GetTrainerServiceIcon)
WHOA_LUA_STUB(GetTrainerServiceInfo)
WHOA_LUA_STUB(GetTrainerServiceItemLink)
WHOA_LUA_STUB(GetTrainerServiceLevelReq)
WHOA_LUA_STUB(GetTrainerServiceNumAbilityReq)
WHOA_LUA_STUB(GetTrainerServiceSkillLine)
WHOA_LUA_STUB(GetTrainerServiceSkillReq)
WHOA_LUA_STUB(GetTrainerServiceStepIncrease)
WHOA_LUA_STUB(GetTrainerServiceStepReq)
WHOA_LUA_STUB(GetTrainerServiceTypeFilter)
WHOA_LUA_STUB(GetTrainerSkillLineFilter)
WHOA_LUA_STUB(GetTrainerSkillLines)
WHOA_LUA_STUB(GetVoiceSessionMemberInfoBySessionID)
WHOA_LUA_STUB(GetWatchedFactionInfo)
WHOA_LUA_STUB(GetWhoInfo)
WHOA_LUA_STUB(GetWintergraspWaitTime)
WHOA_LUA_STUB(GetWorldStateUIInfo)
WHOA_LUA_STUB(GiveMasterLoot)
WHOA_LUA_STUB(GlyphMatchesSocket)
WHOA_LUA_STUB(GuildControlAddRank)
int32_t Script_Stub_GuildControlGetNumRanks(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}
WHOA_LUA_STUB(GuildControlGetRankFlags)
WHOA_LUA_STUB(GuildControlGetRankName)
WHOA_LUA_STUB(GuildControlSaveRank)
WHOA_LUA_STUB(GuildControlSetRank)
WHOA_LUA_STUB(GuildControlSetRankFlag)
WHOA_LUA_STUB(GuildRoster)
WHOA_LUA_STUB(GuildRosterSetOfficerNote)
WHOA_LUA_STUB(GuildRosterSetPublicNote)
WHOA_LUA_STUB(HideRepairCursor)
int32_t Script_Stub_InRepairMode(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
int32_t Script_Stub_InboxItemCanDelete(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
WHOA_LUA_STUB(IsMouselooking)
WHOA_LUA_STUB(ItemTextGetCreator)
WHOA_LUA_STUB(ItemTextGetItem)
WHOA_LUA_STUB(ItemTextGetMaterial)
WHOA_LUA_STUB(ItemTextGetPage)
WHOA_LUA_STUB(ItemTextGetText)
WHOA_LUA_STUB(ItemTextHasNextPage)
WHOA_LUA_STUB(ItemTextNextPage)
WHOA_LUA_STUB(ItemTextPrevPage)
WHOA_LUA_STUB(JoinChannelByName)
WHOA_LUA_STUB(JoinLFG)
WHOA_LUA_STUB(JoinPermanentChannel)
WHOA_LUA_STUB(JoinTemporaryChannel)
WHOA_LUA_STUB(LFGTeleport)
WHOA_LUA_STUB(LearnPreviewTalents)
WHOA_LUA_STUB(LearnTalent)
WHOA_LUA_STUB(LeaveChannelByName)
WHOA_LUA_STUB(LeaveLFG)
WHOA_LUA_STUB(ListChannels)
WHOA_LUA_STUB(LoggingChat)
WHOA_LUA_STUB(LoggingCombat)
WHOA_LUA_STUB(LootSlot)
int32_t Script_Stub_LootSlotIsCoin(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
int32_t Script_Stub_LootSlotIsItem(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
WHOA_LUA_STUB(MakeMinigameMove)
WHOA_LUA_STUB(ManageBackpackTokenFrame)
WHOA_LUA_STUB(MouselookStart)
WHOA_LUA_STUB(MouselookStop)
WHOA_LUA_STUB(NextView)
WHOA_LUA_STUB(NumTaxiNodes)
WHOA_LUA_STUB(OfferPetition)
WHOA_LUA_STUB(OpenCalendar)
WHOA_LUA_STUB(OpenTrainer)
WHOA_LUA_STUB(PartyLFGStartBackfill)
WHOA_LUA_STUB(PetAbandon)
WHOA_LUA_STUB(PetAggressiveMode)
WHOA_LUA_STUB(PetAttack)
int32_t Script_Stub_PetCanBeAbandoned(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
int32_t Script_Stub_PetCanBeDismissed(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
int32_t Script_Stub_PetCanBeRenamed(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
WHOA_LUA_STUB(PetDefensiveMode)
WHOA_LUA_STUB(PetDismiss)
WHOA_LUA_STUB(PetFollow)
WHOA_LUA_STUB(PetPassiveMode)
WHOA_LUA_STUB(PetRename)
WHOA_LUA_STUB(PetStopAttack)
WHOA_LUA_STUB(PetWait)
WHOA_LUA_STUB(PitchDownStart)
WHOA_LUA_STUB(PitchDownStop)
WHOA_LUA_STUB(PitchUpStart)
WHOA_LUA_STUB(PitchUpStop)
WHOA_LUA_STUB(PlaceAuctionBid)
WHOA_LUA_STUB(PlaceGlyphInSocket)
WHOA_LUA_STUB(PlayDance)
WHOA_LUA_STUB(PositionMiniWorldMapArrowFrame)
WHOA_LUA_STUB(PositionWorldMapArrowFrame)
WHOA_LUA_STUB(PrevView)
WHOA_LUA_STUB(ProcessMapClick)
WHOA_LUA_STUB(ProcessQuestLogRewardFactions)
WHOA_LUA_STUB(PurchaseSlot)
WHOA_LUA_STUB(QuestChooseRewardError)
WHOA_LUA_STUB(QuestFlagsPVP)
WHOA_LUA_STUB(QuestGetAutoAccept)
WHOA_LUA_STUB(QuestIsDaily)
WHOA_LUA_STUB(QuestIsWeekly)
WHOA_LUA_STUB(QuestLogPushQuest)
WHOA_LUA_STUB(QuestMapUpdateAllQuests)
WHOA_LUA_STUB(QuestPOIGetIconInfo)
WHOA_LUA_STUB(QuestPOIGetQuestIDByIndex)
WHOA_LUA_STUB(QuestPOIGetQuestIDByVisibleIndex)
WHOA_LUA_STUB(QuestPOIUpdateIcons)
WHOA_LUA_STUB(QuestPOIUpdateTexture)
WHOA_LUA_STUB(RejectProposal)
WHOA_LUA_STUB(RenameEquipmentSet)
WHOA_LUA_STUB(RenamePetition)
WHOA_LUA_STUB(RepairAllItems)
WHOA_LUA_STUB(RespondMailLockSendItem)
WHOA_LUA_STUB(ReturnInboxItem)
WHOA_LUA_STUB(RollOnLoot)
WHOA_LUA_STUB(RunMacro)
WHOA_LUA_STUB(RunMacroText)
WHOA_LUA_STUB(SaveEquipmentSet)
WHOA_LUA_STUB(SaveView)
WHOA_LUA_STUB(SearchLFGGetEncounterResults)
WHOA_LUA_STUB(SearchLFGGetJoinedID)
WHOA_LUA_STUB(SearchLFGGetNumResults)
WHOA_LUA_STUB(SearchLFGGetPartyResults)
WHOA_LUA_STUB(SearchLFGGetResults)
WHOA_LUA_STUB(SearchLFGJoin)
WHOA_LUA_STUB(SearchLFGLeave)
WHOA_LUA_STUB(SearchLFGSort)
WHOA_LUA_STUB(SecureCmdOptionParse)
WHOA_LUA_STUB(SelectActiveQuest)
WHOA_LUA_STUB(SelectAvailableQuest)
WHOA_LUA_STUB(SelectGossipActiveQuest)
WHOA_LUA_STUB(SelectGossipAvailableQuest)
WHOA_LUA_STUB(SelectGossipOption)
WHOA_LUA_STUB(SelectPackage)
WHOA_LUA_STUB(SelectStationery)
WHOA_LUA_STUB(SelectTradeSkill)
WHOA_LUA_STUB(SelectTrainerService)
WHOA_LUA_STUB(ShiftQuestWatches)
WHOA_LUA_STUB(ShowBuybackSellCursor)
WHOA_LUA_STUB(ShowContainerSellCursor)
WHOA_LUA_STUB(ShowFriends)
WHOA_LUA_STUB(ShowMerchantSellCursor)
WHOA_LUA_STUB(ShowMiniWorldMapArrowFrame)
WHOA_LUA_STUB(ShowQuickButton)
WHOA_LUA_STUB(ShowRepairCursor)
WHOA_LUA_STUB(ShowWorldMapArrowFrame)
WHOA_LUA_STUB(SignPetition)
WHOA_LUA_STUB(SocketContainerItem)
WHOA_LUA_STUB(SpellCanTargetGlyph)
int32_t Script_Stub_SpellCanTargetItem(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
WHOA_LUA_STUB(SpellCanTargetUnit)
WHOA_LUA_STUB(SpellHasRange)
int32_t Script_Stub_SpellIsTargeting(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
WHOA_LUA_STUB(SpellStopCasting)
WHOA_LUA_STUB(SpellStopTargeting)
WHOA_LUA_STUB(SpellTargetItem)
WHOA_LUA_STUB(SpellTargetUnit)
WHOA_LUA_STUB(SplitContainerItem)
WHOA_LUA_STUB(SplitGuildBankItem)
WHOA_LUA_STUB(StablePet)
WHOA_LUA_STUB(StrafeLeftStart)
WHOA_LUA_STUB(StrafeLeftStop)
WHOA_LUA_STUB(StrafeRightStart)
WHOA_LUA_STUB(StrafeRightStop)
WHOA_LUA_STUB(SummonRandomCritter)
WHOA_LUA_STUB(TakeInboxItem)
WHOA_LUA_STUB(TakeInboxMoney)
WHOA_LUA_STUB(TakeInboxTextItem)
WHOA_LUA_STUB(TakeTaxiNode)
WHOA_LUA_STUB(TaxiGetDestX)
WHOA_LUA_STUB(TaxiGetDestY)
WHOA_LUA_STUB(TaxiGetSrcX)
WHOA_LUA_STUB(TaxiGetSrcY)
WHOA_LUA_STUB(TaxiNodeCost)
WHOA_LUA_STUB(TaxiNodeGetType)
WHOA_LUA_STUB(TaxiNodeName)
WHOA_LUA_STUB(TaxiNodePosition)
WHOA_LUA_STUB(TaxiNodeSetCurrent)
WHOA_LUA_STUB(TeleportToDebugObject)
WHOA_LUA_STUB(TradeSkillOnlyShowMakeable)
WHOA_LUA_STUB(TradeSkillOnlyShowSkillUps)
int32_t Script_Stub_UnitHasLFGDeserter(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
int32_t Script_Stub_UnitHasLFGRandomCooldown(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
int32_t Script_Stub_UnitIsSilenced(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}
int32_t Script_Stub_UnitIsTalking(lua_State* L) {
    // Voice chat is not implemented, so nobody is ever talking.
    lua_pushboolean(L, 0);

    return 1;
}
WHOA_LUA_STUB(UnstablePet)
WHOA_LUA_STUB(UpdateMapHighlight)
WHOA_LUA_STUB(UpdateWorldMapArrowFrames)
WHOA_LUA_STUB(VehicleAimDecrement)
WHOA_LUA_STUB(VehicleAimDownStart)
WHOA_LUA_STUB(VehicleAimDownStop)
WHOA_LUA_STUB(VehicleAimGetAngle)
WHOA_LUA_STUB(VehicleAimGetNormAngle)
WHOA_LUA_STUB(VehicleAimGetNormPower)
WHOA_LUA_STUB(VehicleAimIncrement)
WHOA_LUA_STUB(VehicleAimRequestAngle)
WHOA_LUA_STUB(VehicleAimRequestNormAngle)
WHOA_LUA_STUB(VehicleAimSetNormPower)
WHOA_LUA_STUB(VehicleAimUpStart)
WHOA_LUA_STUB(VehicleAimUpStop)
WHOA_LUA_STUB(VehicleCameraZoomIn)
WHOA_LUA_STUB(VehicleCameraZoomOut)
WHOA_LUA_STUB(VehicleExit)
WHOA_LUA_STUB(VehicleNextSeat)
WHOA_LUA_STUB(VehiclePrevSeat)
WHOA_LUA_STUB(VoiceEnumerateCaptureDevices)
WHOA_LUA_STUB(VoiceEnumerateOutputDevices)
WHOA_LUA_STUB(VoiceGetCurrentCaptureDevice)
WHOA_LUA_STUB(VoiceGetCurrentOutputDevice)
WHOA_LUA_STUB(VoiceSelectCaptureDevice)
WHOA_LUA_STUB(VoiceSelectOutputDevice)
WHOA_LUA_STUB(WithdrawGuildBankMoney)
WHOA_LUA_STUB(ZoomOut)

struct ScriptFunction {
    const char* name;
    int32_t (*method)(lua_State*);
};

const ScriptFunction s_stubs[] = {
    { "AcceptDuel",                              &Script_Stub_AcceptDuel },
    { "AcceptProposal",                          &Script_Stub_AcceptProposal },
    { "AcceptQuest",                             &Script_Stub_AcceptQuest },
    { "AcceptSkillUps",                          &Script_Stub_AcceptSkillUps },
    { "AcceptSockets",                           &Script_Stub_AcceptSockets },
    { "AddChatWindowChannel",                    &Script_Stub_AddChatWindowChannel },
    { "AddChatWindowMessages",                   &Script_Stub_AddChatWindowMessages },
    { "AddFriend",                               &Script_Stub_AddFriend },
    { "AddIgnore",                               &Script_Stub_AddIgnore },
    { "AddMute",                                 &Script_Stub_AddMute },
    { "AddOrDelIgnore",                          &Script_Stub_AddOrDelIgnore },
    { "AddOrDelMute",                            &Script_Stub_AddOrDelMute },
    { "AddOrRemoveFriend",                       &Script_Stub_AddOrRemoveFriend },
    { "AddPreviewTalentPoints",                  &Script_Stub_AddPreviewTalentPoints },
    { "AddQuestWatch",                           &Script_Stub_AddQuestWatch },
    { "AddSkillUp",                              &Script_Stub_AddSkillUp },
    { "AddTrackedAchievement",                   &Script_Stub_AddTrackedAchievement },
    { "AscendStop",                              &Script_Stub_AscendStop },
    { "BuyGuildBankTab",                         &Script_Stub_BuyGuildBankTab },
    { "BuyGuildCharter",                         &Script_Stub_BuyGuildCharter },
    { "BuyMerchantItem",                         &Script_Stub_BuyMerchantItem },
    { "BuyPetition",                             &Script_Stub_BuyPetition },
    { "BuySkillTier",                            &Script_Stub_BuySkillTier },
    { "BuyStableSlot",                           &Script_Stub_BuyStableSlot },
    { "BuyTrainerService",                       &Script_Stub_BuyTrainerService },
    { "BuybackItem",                             &Script_Stub_BuybackItem },
    { "ClearAchievementComparisonUnit",          &Script_Stub_ClearAchievementComparisonUnit },
    { "ClearAllLFGDungeons",                     &Script_Stub_ClearAllLFGDungeons },
    { "ClearChannelWatch",                       &Script_Stub_ClearChannelWatch },
    { "ClearLFGDungeon",                         &Script_Stub_ClearLFGDungeon },
    { "ClearSendMail",                           &Script_Stub_ClearSendMail },
    { "ClearTutorials",                          &Script_Stub_ClearTutorials },
    { "CloseArenaTeamRoster",                    &Script_Stub_CloseArenaTeamRoster },
    { "CloseAuctionHouse",                       &Script_Stub_CloseAuctionHouse },
    { "CloseBankFrame",                          &Script_Stub_CloseBankFrame },
    { "CloseGossip",                             &Script_Stub_CloseGossip },
    { "CloseGuildBankFrame",                     &Script_Stub_CloseGuildBankFrame },
    { "CloseGuildRegistrar",                     &Script_Stub_CloseGuildRegistrar },
    { "CloseGuildRoster",                        &Script_Stub_CloseGuildRoster },
    { "CloseItemText",                           &Script_Stub_CloseItemText },
    { "CloseLoot",                               &Script_Stub_CloseLoot },
    { "CloseMail",                               &Script_Stub_CloseMail },
    { "CloseMerchant",                           &Script_Stub_CloseMerchant },
    { "ClosePetStables",                         &Script_Stub_ClosePetStables },
    { "ClosePetition",                           &Script_Stub_ClosePetition },
    { "ClosePetitionVendor",                     &Script_Stub_ClosePetitionVendor },
    { "CloseQuest",                              &Script_Stub_CloseQuest },
    { "CloseSocketInfo",                         &Script_Stub_CloseSocketInfo },
    { "CloseTabardCreation",                     &Script_Stub_CloseTabardCreation },
    { "CloseTaxiMap",                            &Script_Stub_CloseTaxiMap },
    { "CloseTradeSkill",                         &Script_Stub_CloseTradeSkill },
    { "CloseTrainer",                            &Script_Stub_CloseTrainer },
    { "ConfirmAcceptQuest",                      &Script_Stub_ConfirmAcceptQuest },
    { "ConfirmLootRoll",                         &Script_Stub_ConfirmLootRoll },
    { "ConfirmLootSlot",                         &Script_Stub_ConfirmLootSlot },
    { "DeclineQuest",                            &Script_Stub_DeclineQuest },
    { "DescendStop",                             &Script_Stub_DescendStop },
    { "JumpOrAscendStart",                       &Script_Stub_JumpOrAscendStart },
    { "MoveAndSteerStart",                       &Script_Stub_MoveAndSteerStart },
    { "MoveAndSteerStop",                        &Script_Stub_MoveAndSteerStop },
    { "MoveBackwardStart",                       &Script_Stub_MoveBackwardStart },
    { "MoveBackwardStop",                        &Script_Stub_MoveBackwardStop },
    { "MoveForwardStart",                        &Script_Stub_MoveForwardStart },
    { "MoveForwardStop",                         &Script_Stub_MoveForwardStop },
    { "MoveViewDownStart",                       &Script_Stub_MoveViewDownStart },
    { "MoveViewDownStop",                        &Script_Stub_MoveViewDownStop },
    { "MoveViewInStart",                         &Script_Stub_MoveViewInStart },
    { "MoveViewInStop",                          &Script_Stub_MoveViewInStop },
    { "MoveViewLeftStart",                       &Script_Stub_MoveViewLeftStart },
    { "MoveViewLeftStop",                        &Script_Stub_MoveViewLeftStop },
    { "MoveViewOutStart",                        &Script_Stub_MoveViewOutStart },
    { "MoveViewOutStop",                         &Script_Stub_MoveViewOutStop },
    { "MoveViewRightStart",                      &Script_Stub_MoveViewRightStart },
    { "MoveViewRightStop",                       &Script_Stub_MoveViewRightStop },
    { "MoveViewUpStart",                         &Script_Stub_MoveViewUpStart },
    { "MoveViewUpStop",                          &Script_Stub_MoveViewUpStop },
    { "PickupCompanion",                         &Script_Stub_PickupCompanion },
    { "PickupContainerItem",                     &Script_Stub_PickupContainerItem },
    { "PickupEquipmentSet",                      &Script_Stub_PickupEquipmentSet },
    { "PickupEquipmentSetByName",                &Script_Stub_PickupEquipmentSetByName },
    { "PickupGuildBankItem",                     &Script_Stub_PickupGuildBankItem },
    { "PickupGuildBankMoney",                    &Script_Stub_PickupGuildBankMoney },
    { "PickupMacro",                             &Script_Stub_PickupMacro },
    { "PickupMerchantItem",                      &Script_Stub_PickupMerchantItem },
    { "PickupPetAction",                         &Script_Stub_PickupPetAction },
    { "PickupSpell",                             &Script_Stub_PickupSpell },
    { "PickupStablePet",                         &Script_Stub_PickupStablePet },
    { "QueryAuctionItems",                       &Script_Stub_QueryAuctionItems },
    { "QueryGuildBankLog",                       &Script_Stub_QueryGuildBankLog },
    { "QueryGuildBankTab",                       &Script_Stub_QueryGuildBankTab },
    { "QueryGuildBankText",                      &Script_Stub_QueryGuildBankText },
    { "QueryGuildEventLog",                      &Script_Stub_QueryGuildEventLog },
    { "QueryQuestsCompleted",                    &Script_Stub_QueryQuestsCompleted },
    { "RefreshLFGList",                          &Script_Stub_RefreshLFGList },
    { "RemoveChatWindowChannel",                 &Script_Stub_RemoveChatWindowChannel },
    { "RemoveChatWindowMessages",                &Script_Stub_RemoveChatWindowMessages },
    { "RemoveFriend",                            &Script_Stub_RemoveFriend },
    { "RemoveGlyphFromSocket",                   &Script_Stub_RemoveGlyphFromSocket },
    { "RemoveQuestWatch",                        &Script_Stub_RemoveQuestWatch },
    { "RemoveSkillUp",                           &Script_Stub_RemoveSkillUp },
    { "RemoveTrackedAchievement",                &Script_Stub_RemoveTrackedAchievement },
    { "RequestLFDPartyLockInfo",                 &Script_Stub_RequestLFDPartyLockInfo },
    { "RequestLFDPlayerLockInfo",                &Script_Stub_RequestLFDPlayerLockInfo },
    { "ResetChatColors",                         &Script_Stub_ResetChatColors },
    { "ResetChatWindows",                        &Script_Stub_ResetChatWindows },
    { "ResetGroupPreviewTalentPoints",           &Script_Stub_ResetGroupPreviewTalentPoints },
    { "ResetPreviewTalentPoints",                &Script_Stub_ResetPreviewTalentPoints },
    { "ResetTutorials",                          &Script_Stub_ResetTutorials },
    { "ResetView",                               &Script_Stub_ResetView },
    { "SendAddonMessage",                        &Script_Stub_SendAddonMessage },
    { "SendChatMessage",                         &Script_Stub_SendChatMessage },
    { "SendMail",                                &Script_Stub_SendMail },
    { "SendSystemMessage",                       &Script_Stub_SendSystemMessage },
    { "SendWho",                                 &Script_Stub_SendWho },
    { "SetAchievementComparisonUnit",            &Script_Stub_SetAchievementComparisonUnit },
    { "SetActiveTalentGroup",                    &Script_Stub_SetActiveTalentGroup },
    { "SetActiveVoiceChannel",                   &Script_Stub_SetActiveVoiceChannel },
    { "SetActiveVoiceChannelBySessionID",        &Script_Stub_SetActiveVoiceChannelBySessionID },
    { "SetArenaTeamRosterSelection",             &Script_Stub_SetArenaTeamRosterSelection },
    { "SetArenaTeamRosterShowOffline",           &Script_Stub_SetArenaTeamRosterShowOffline },
    { "SetAuctionsTabShowing",                   &Script_Stub_SetAuctionsTabShowing },
    { "SetChannelPassword",                      &Script_Stub_SetChannelPassword },
    { "SetChatColorNameByClass",                 &Script_Stub_SetChatColorNameByClass },
    { "SetChatWindowAlpha",                      &Script_Stub_SetChatWindowAlpha },
    { "SetChatWindowColor",                      &Script_Stub_SetChatWindowColor },
    { "SetChatWindowDocked",                     &Script_Stub_SetChatWindowDocked },
    { "SetChatWindowLocked",                     &Script_Stub_SetChatWindowLocked },
    { "SetChatWindowName",                       &Script_Stub_SetChatWindowName },
    { "SetChatWindowSavedDimensions",            &Script_Stub_SetChatWindowSavedDimensions },
    { "SetChatWindowSavedPosition",              &Script_Stub_SetChatWindowSavedPosition },
    { "SetChatWindowShown",                      &Script_Stub_SetChatWindowShown },
    { "SetChatWindowSize",                       &Script_Stub_SetChatWindowSize },
    { "SetChatWindowUninteractable",             &Script_Stub_SetChatWindowUninteractable },
    { "SetCurrencyBackpack",                     &Script_Stub_SetCurrencyBackpack },
    { "SetCurrencyUnused",                       &Script_Stub_SetCurrencyUnused },
    { "SetCurrentGuildBankTab",                  &Script_Stub_SetCurrentGuildBankTab },
    { "SetDungeonMapLevel",                      &Script_Stub_SetDungeonMapLevel },
    { "SetFactionActive",                        &Script_Stub_SetFactionActive },
    { "SetFactionInactive",                      &Script_Stub_SetFactionInactive },
    { "SetFriendNotes",                          &Script_Stub_SetFriendNotes },
    { "SetGuildBankTabInfo",                     &Script_Stub_SetGuildBankTabInfo },
    { "SetGuildBankTabPermissions",              &Script_Stub_SetGuildBankTabPermissions },
    { "SetGuildBankTabWithdraw",                 &Script_Stub_SetGuildBankTabWithdraw },
    { "SetGuildBankText",                        &Script_Stub_SetGuildBankText },
    { "SetGuildBankWithdrawLimit",               &Script_Stub_SetGuildBankWithdrawLimit },
    { "SetGuildInfoText",                        &Script_Stub_SetGuildInfoText },
    { "SetGuildRosterShowOffline",               &Script_Stub_SetGuildRosterShowOffline },
    { "SetLFGBootVote",                          &Script_Stub_SetLFGBootVote },
    { "SetLFGComment",                           &Script_Stub_SetLFGComment },
    { "SetLFGDungeon",                           &Script_Stub_SetLFGDungeon },
    { "SetLFGDungeonEnabled",                    &Script_Stub_SetLFGDungeonEnabled },
    { "SetLFGHeaderCollapsed",                   &Script_Stub_SetLFGHeaderCollapsed },
    { "SetLFGRoles",                             &Script_Stub_SetLFGRoles },
    { "SetLootPortrait",                         &Script_Stub_SetLootPortrait },
    { "SetMacroItem",                            &Script_Stub_SetMacroItem },
    { "SetMacroSpell",                           &Script_Stub_SetMacroSpell },
    { "SetMapByID",                              &Script_Stub_SetMapByID },
    { "SetMapZoom",                              &Script_Stub_SetMapZoom },
    { "SetMouselookOverrideBinding",             &Script_Stub_SetMouselookOverrideBinding },
    { "SetNextBarberShopStyle",                  &Script_Stub_SetNextBarberShopStyle },
    { "SetPOIIconOverlapDistance",               &Script_Stub_SetPOIIconOverlapDistance },
    { "SetPOIIconOverlapPushDistance",           &Script_Stub_SetPOIIconOverlapPushDistance },
    { "SetPetStablePaperdoll",                   &Script_Stub_SetPetStablePaperdoll },
    { "SetSavedInstanceExtend",                  &Script_Stub_SetSavedInstanceExtend },
    { "SetSelectedAuctionItem",                  &Script_Stub_SetSelectedAuctionItem },
    { "SetSelectedDisplayChannel",               &Script_Stub_SetSelectedDisplayChannel },
    { "SetSelectedFaction",                      &Script_Stub_SetSelectedFaction },
    { "SetSelectedFriend",                       &Script_Stub_SetSelectedFriend },
    { "SetSelectedIgnore",                       &Script_Stub_SetSelectedIgnore },
    { "SetSelectedMute",                         &Script_Stub_SetSelectedMute },
    { "SetSendMailCOD",                          &Script_Stub_SetSendMailCOD },
    { "SetSendMailMoney",                        &Script_Stub_SetSendMailMoney },
    { "SetSendMailShowing",                      &Script_Stub_SetSendMailShowing },
    { "SetTaxiMap",                              &Script_Stub_SetTaxiMap },
    { "SetTradeSkillInvSlotFilter",              &Script_Stub_SetTradeSkillInvSlotFilter },
    { "SetTradeSkillItemLevelFilter",            &Script_Stub_SetTradeSkillItemLevelFilter },
    { "SetTradeSkillItemNameFilter",             &Script_Stub_SetTradeSkillItemNameFilter },
    { "SetTradeSkillSubClassFilter",             &Script_Stub_SetTradeSkillSubClassFilter },
    { "SetTrainerServiceTypeFilter",             &Script_Stub_SetTrainerServiceTypeFilter },
    { "SetTrainerSkillLineFilter",               &Script_Stub_SetTrainerSkillLineFilter },
    { "SetView",                                 &Script_Stub_SetView },
    { "SetWatchedFactionIndex",                  &Script_Stub_SetWatchedFactionIndex },
    { "SortArenaTeamRoster",                     &Script_Stub_SortArenaTeamRoster },
    { "SortAuctionApplySort",                    &Script_Stub_SortAuctionApplySort },
    { "SortAuctionClearSort",                    &Script_Stub_SortAuctionClearSort },
    { "SortAuctionItems",                        &Script_Stub_SortAuctionItems },
    { "SortAuctionSetSort",                      &Script_Stub_SortAuctionSetSort },
    { "SortGuildRoster",                         &Script_Stub_SortGuildRoster },
    { "SortQuestWatches",                        &Script_Stub_SortQuestWatches },
    { "SortWho",                                 &Script_Stub_SortWho },
    { "StartAuction",                            &Script_Stub_StartAuction },
    { "StartDuel",                               &Script_Stub_StartDuel },
    { "StopMacro",                               &Script_Stub_StopMacro },
    { "StopTradeSkillRepeat",                    &Script_Stub_StopTradeSkillRepeat },
    { "StopwatchCloseButton_OnClick",            &Script_Stub_StopwatchCloseButton_OnClick },
    { "StopwatchFrame_OnDragStart",              &Script_Stub_StopwatchFrame_OnDragStart },
    { "StopwatchFrame_OnDragStop",               &Script_Stub_StopwatchFrame_OnDragStop },
    { "StopwatchFrame_OnEvent",                  &Script_Stub_StopwatchFrame_OnEvent },
    { "StopwatchFrame_OnHide",                   &Script_Stub_StopwatchFrame_OnHide },
    { "StopwatchFrame_OnLoad",                   &Script_Stub_StopwatchFrame_OnLoad },
    { "StopwatchFrame_OnMouseDown",              &Script_Stub_StopwatchFrame_OnMouseDown },
    { "StopwatchFrame_OnMouseUp",                &Script_Stub_StopwatchFrame_OnMouseUp },
    { "StopwatchFrame_OnShow",                   &Script_Stub_StopwatchFrame_OnShow },
    { "StopwatchFrame_OnUpdate",                 &Script_Stub_StopwatchFrame_OnUpdate },
    { "StopwatchPlayPauseButton_OnClick",        &Script_Stub_StopwatchPlayPauseButton_OnClick },
    { "StopwatchResetButton_OnClick",            &Script_Stub_StopwatchResetButton_OnClick },
    { "StopwatchTicker_OnUpdate",                &Script_Stub_StopwatchTicker_OnUpdate },
    { "StopwatchTicker_Update",                  &Script_Stub_StopwatchTicker_Update },
    { "Stopwatch_Clear",                         &Script_Stub_Stopwatch_Clear },
    { "Stopwatch_FinishCountdown",               &Script_Stub_Stopwatch_FinishCountdown },
    { "Stopwatch_IsPlaying",                     &Script_Stub_Stopwatch_IsPlaying },
    { "Stopwatch_Pause",                         &Script_Stub_Stopwatch_Pause },
    { "Stopwatch_Play",                          &Script_Stub_Stopwatch_Play },
    { "Stopwatch_StartCountdown",                &Script_Stub_Stopwatch_StartCountdown },
    { "Stopwatch_Toggle",                        &Script_Stub_Stopwatch_Toggle },
    { "ToggleAutoRun",                           &Script_Stub_ToggleAutoRun },
    { "TogglePetAutocast",                       &Script_Stub_TogglePetAutocast },
    { "ToggleRun",                               &Script_Stub_ToggleRun },
    { "ToggleSpellAutocast",                     &Script_Stub_ToggleSpellAutocast },
    { "TurnInArenaPetition",                     &Script_Stub_TurnInArenaPetition },
    { "TurnInGuildCharter",                      &Script_Stub_TurnInGuildCharter },
    { "TurnInPetition",                          &Script_Stub_TurnInPetition },
    { "TurnLeftStart",                           &Script_Stub_TurnLeftStart },
    { "TurnLeftStop",                            &Script_Stub_TurnLeftStop },
    { "TurnOrActionStart",                       &Script_Stub_TurnOrActionStart },
    { "TurnOrActionStop",                        &Script_Stub_TurnOrActionStop },
    { "TurnRightStart",                          &Script_Stub_TurnRightStart },
    { "TurnRightStop",                           &Script_Stub_TurnRightStop },
    { "UseContainerItem",                        &Script_Stub_UseContainerItem },
    { "UseEquipmentSet",                         &Script_Stub_UseEquipmentSet },
    { "UseQuestLogSpecialItem",                  &Script_Stub_UseQuestLogSpecialItem },
    { "AbandonQuest",                            &Script_Stub_AbandonQuest },
    { "AbandonSkill",                            &Script_Stub_AbandonSkill },
    { "ApplyBarberShopStyle",                    &Script_Stub_ApplyBarberShopStyle },
    { "ArenaTeamRoster",                         &Script_Stub_ArenaTeamRoster },
    { "AutoLootMailItem",                        &Script_Stub_AutoLootMailItem },
    { "AutoStoreGuildBankItem",                  &Script_Stub_AutoStoreGuildBankItem },
    { "BankButtonIDToInvSlotID",                 &Script_Stub_BankButtonIDToInvSlotID },
    { "BarberShopReset",                         &Script_Stub_BarberShopReset },
    { "CalculateAuctionDeposit",                 &Script_Stub_CalculateAuctionDeposit },
    { "CalendarAddEvent",                        &Script_Stub_CalendarAddEvent },
    { "CalendarCanAddEvent",                     &Script_Stub_CalendarCanAddEvent },
    { "CalendarCanSendInvite",                   &Script_Stub_CalendarCanSendInvite },
    { "CalendarCloseEvent",                      &Script_Stub_CalendarCloseEvent },
    { "CalendarContextDeselectEvent",            &Script_Stub_CalendarContextDeselectEvent },
    { "CalendarContextEventCanComplain",         &Script_Stub_CalendarContextEventCanComplain },
    { "CalendarContextEventCanEdit",             &Script_Stub_CalendarContextEventCanEdit },
    { "CalendarContextEventClipboard",           &Script_Stub_CalendarContextEventClipboard },
    { "CalendarContextEventComplain",            &Script_Stub_CalendarContextEventComplain },
    { "CalendarContextEventCopy",                &Script_Stub_CalendarContextEventCopy },
    { "CalendarContextEventGetCalendarType",     &Script_Stub_CalendarContextEventGetCalendarType },
    { "CalendarContextEventPaste",               &Script_Stub_CalendarContextEventPaste },
    { "CalendarContextEventRemove",              &Script_Stub_CalendarContextEventRemove },
    { "CalendarContextEventSignUp",              &Script_Stub_CalendarContextEventSignUp },
    { "CalendarContextGetEventIndex",            &Script_Stub_CalendarContextGetEventIndex },
    { "CalendarContextInviteAvailable",          &Script_Stub_CalendarContextInviteAvailable },
    { "CalendarContextInviteDecline",            &Script_Stub_CalendarContextInviteDecline },
    { "CalendarContextInviteIsPending",          &Script_Stub_CalendarContextInviteIsPending },
    { "CalendarContextInviteModeratorStatus",    &Script_Stub_CalendarContextInviteModeratorStatus },
    { "CalendarContextInviteRemove",             &Script_Stub_CalendarContextInviteRemove },
    { "CalendarContextInviteStatus",             &Script_Stub_CalendarContextInviteStatus },
    { "CalendarContextInviteTentative",          &Script_Stub_CalendarContextInviteTentative },
    { "CalendarContextInviteType",               &Script_Stub_CalendarContextInviteType },
    { "CalendarContextSelectEvent",              &Script_Stub_CalendarContextSelectEvent },
    { "CalendarDefaultGuildFilter",              &Script_Stub_CalendarDefaultGuildFilter },
    { "CalendarEventAvailable",                  &Script_Stub_CalendarEventAvailable },
    { "CalendarEventCanEdit",                    &Script_Stub_CalendarEventCanEdit },
    { "CalendarEventCanModerate",                &Script_Stub_CalendarEventCanModerate },
    { "CalendarEventClearAutoApprove",           &Script_Stub_CalendarEventClearAutoApprove },
    { "CalendarEventClearLocked",                &Script_Stub_CalendarEventClearLocked },
    { "CalendarEventClearModerator",             &Script_Stub_CalendarEventClearModerator },
    { "CalendarEventDecline",                    &Script_Stub_CalendarEventDecline },
    { "CalendarEventGetCalendarType",            &Script_Stub_CalendarEventGetCalendarType },
    { "CalendarEventGetInvite",                  &Script_Stub_CalendarEventGetInvite },
    { "CalendarEventGetInviteResponseTime",      &Script_Stub_CalendarEventGetInviteResponseTime },
    { "CalendarEventGetRepeatOptions",           &Script_Stub_CalendarEventGetRepeatOptions },
    { "CalendarEventGetSelectedInvite",          &Script_Stub_CalendarEventGetSelectedInvite },
    { "CalendarEventGetStatusOptions",           &Script_Stub_CalendarEventGetStatusOptions },
    { "CalendarEventGetTextures",                &Script_Stub_CalendarEventGetTextures },
    { "CalendarEventGetTypes",                   &Script_Stub_CalendarEventGetTypes },
    { "CalendarEventHasPendingInvite",           &Script_Stub_CalendarEventHasPendingInvite },
    { "CalendarEventInvite",                     &Script_Stub_CalendarEventInvite },
    { "CalendarEventIsModerator",                &Script_Stub_CalendarEventIsModerator },
    { "CalendarEventRemoveInvite",               &Script_Stub_CalendarEventRemoveInvite },
    { "CalendarEventSelectInvite",               &Script_Stub_CalendarEventSelectInvite },
    { "CalendarEventSetAutoApprove",             &Script_Stub_CalendarEventSetAutoApprove },
    { "CalendarEventSetDate",                    &Script_Stub_CalendarEventSetDate },
    { "CalendarEventSetDescription",             &Script_Stub_CalendarEventSetDescription },
    { "CalendarEventSetLocked",                  &Script_Stub_CalendarEventSetLocked },
    { "CalendarEventSetLockoutDate",             &Script_Stub_CalendarEventSetLockoutDate },
    { "CalendarEventSetLockoutTime",             &Script_Stub_CalendarEventSetLockoutTime },
    { "CalendarEventSetModerator",               &Script_Stub_CalendarEventSetModerator },
    { "CalendarEventSetRepeatOption",            &Script_Stub_CalendarEventSetRepeatOption },
    { "CalendarEventSetSize",                    &Script_Stub_CalendarEventSetSize },
    { "CalendarEventSetStatus",                  &Script_Stub_CalendarEventSetStatus },
    { "CalendarEventSetTextureID",               &Script_Stub_CalendarEventSetTextureID },
    { "CalendarEventSetTime",                    &Script_Stub_CalendarEventSetTime },
    { "CalendarEventSetTitle",                   &Script_Stub_CalendarEventSetTitle },
    { "CalendarEventSetType",                    &Script_Stub_CalendarEventSetType },
    { "CalendarEventSignUp",                     &Script_Stub_CalendarEventSignUp },
    { "CalendarEventSortInvites",                &Script_Stub_CalendarEventSortInvites },
    { "CalendarEventTentative",                  &Script_Stub_CalendarEventTentative },
    { "CalendarGetAbsMonth",                     &Script_Stub_CalendarGetAbsMonth },
    { "CalendarGetDayEvent",                     &Script_Stub_CalendarGetDayEvent },
    { "CalendarGetDayEventSequenceInfo",         &Script_Stub_CalendarGetDayEventSequenceInfo },
    { "CalendarGetEventIndex",                   &Script_Stub_CalendarGetEventIndex },
    { "CalendarGetEventInfo",                    &Script_Stub_CalendarGetEventInfo },
    { "CalendarGetFirstPendingInvite",           &Script_Stub_CalendarGetFirstPendingInvite },
    { "CalendarGetHolidayInfo",                  &Script_Stub_CalendarGetHolidayInfo },
    { "CalendarGetMaxCreateDate",                &Script_Stub_CalendarGetMaxCreateDate },
    { "CalendarGetMaxDate",                      &Script_Stub_CalendarGetMaxDate },
    { "CalendarGetMinDate",                      &Script_Stub_CalendarGetMinDate },
    { "CalendarGetMinHistoryDate",               &Script_Stub_CalendarGetMinHistoryDate },
    { "CalendarGetMonth",                        &Script_Stub_CalendarGetMonth },
    { "CalendarGetMonthNames",                   &Script_Stub_CalendarGetMonthNames },
    { "CalendarGetNumDayEvents",                 &Script_Stub_CalendarGetNumDayEvents },
    { "CalendarGetRaidInfo",                     &Script_Stub_CalendarGetRaidInfo },
    { "CalendarGetWeekdayNames",                 &Script_Stub_CalendarGetWeekdayNames },
    { "CalendarMassInviteArenaTeam",             &Script_Stub_CalendarMassInviteArenaTeam },
    { "CalendarMassInviteGuild",                 &Script_Stub_CalendarMassInviteGuild },
    { "CalendarNewEvent",                        &Script_Stub_CalendarNewEvent },
    { "CalendarNewGuildAnnouncement",            &Script_Stub_CalendarNewGuildAnnouncement },
    { "CalendarNewGuildEvent",                   &Script_Stub_CalendarNewGuildEvent },
    { "CalendarOpenEvent",                       &Script_Stub_CalendarOpenEvent },
    { "CalendarRemoveEvent",                     &Script_Stub_CalendarRemoveEvent },
    { "CalendarSetAbsMonth",                     &Script_Stub_CalendarSetAbsMonth },
    { "CalendarSetMonth",                        &Script_Stub_CalendarSetMonth },
    { "CalendarUpdateEvent",                     &Script_Stub_CalendarUpdateEvent },
    { "CallCompanion",                           &Script_Stub_CallCompanion },
    { "CameraOrSelectOrMoveStart",               &Script_Stub_CameraOrSelectOrMoveStart },
    { "CameraOrSelectOrMoveStop",                &Script_Stub_CameraOrSelectOrMoveStop },
    { "CameraZoomIn",                            &Script_Stub_CameraZoomIn },
    { "CameraZoomOut",                           &Script_Stub_CameraZoomOut },
    { "CastPetAction",                           &Script_Stub_CastPetAction },
    { "CastShapeshiftForm",                      &Script_Stub_CastShapeshiftForm },
    { "CastSpellByID",                           &Script_Stub_CastSpellByID },
    { "CastSpellByName",                         &Script_Stub_CastSpellByName },
    { "ChangeChatColor",                         &Script_Stub_ChangeChatColor },
    { "ChannelSilenceAll",                       &Script_Stub_ChannelSilenceAll },
    { "ChannelSilenceVoice",                     &Script_Stub_ChannelSilenceVoice },
    { "ChannelUnSilenceAll",                     &Script_Stub_ChannelUnSilenceAll },
    { "ChannelUnSilenceVoice",                   &Script_Stub_ChannelUnSilenceVoice },
    { "CheckInbox",                              &Script_Stub_CheckInbox },
    { "ClickAuctionSellItemButton",              &Script_Stub_ClickAuctionSellItemButton },
    { "ClickLandmark",                           &Script_Stub_ClickLandmark },
    { "ClickPetitionButton",                     &Script_Stub_ClickPetitionButton },
    { "ClickSendMailItemButton",                 &Script_Stub_ClickSendMailItemButton },
    { "ClickSocketButton",                       &Script_Stub_ClickSocketButton },
    { "ClickStablePet",                          &Script_Stub_ClickStablePet },
    { "CollapseAllFactionHeaders",               &Script_Stub_CollapseAllFactionHeaders },
    { "CollapseChannelHeader",                   &Script_Stub_CollapseChannelHeader },
    { "CollapseFactionHeader",                   &Script_Stub_CollapseFactionHeader },
    { "CollapseQuestHeader",                     &Script_Stub_CollapseQuestHeader },
    { "CollapseSkillHeader",                     &Script_Stub_CollapseSkillHeader },
    { "CollapseTradeSkillSubClass",              &Script_Stub_CollapseTradeSkillSubClass },
    { "CollapseTrainerSkillLine",                &Script_Stub_CollapseTrainerSkillLine },
    { "CombatLogAddFilter",                      &Script_Stub_CombatLogAddFilter },
    { "CombatLogAdvanceEntry",                   &Script_Stub_CombatLogAdvanceEntry },
    { "CombatLogClearEntries",                   &Script_Stub_CombatLogClearEntries },
    { "CombatLogGetCurrentEntry",                &Script_Stub_CombatLogGetCurrentEntry },
    { "CombatLogGetNumEntries",                  &Script_Stub_CombatLogGetNumEntries },
    { "CombatLogGetRetentionTime",               &Script_Stub_CombatLogGetRetentionTime },
    { "CombatLogResetFilter",                    &Script_Stub_CombatLogResetFilter },
    { "CombatLogSetCurrentEntry",                &Script_Stub_CombatLogSetCurrentEntry },
    { "CombatLogSetRetentionTime",               &Script_Stub_CombatLogSetRetentionTime },
    { "CombatTextSetActiveUnit",                 &Script_Stub_CombatTextSetActiveUnit },
    { "CommentatorAddPlayer",                    &Script_Stub_CommentatorAddPlayer },
    { "CommentatorEnterInstance",                &Script_Stub_CommentatorEnterInstance },
    { "CommentatorExitInstance",                 &Script_Stub_CommentatorExitInstance },
    { "CommentatorFollowPlayer",                 &Script_Stub_CommentatorFollowPlayer },
    { "CommentatorGetCamera",                    &Script_Stub_CommentatorGetCamera },
    { "CommentatorGetCurrentMapID",              &Script_Stub_CommentatorGetCurrentMapID },
    { "CommentatorGetInstanceInfo",              &Script_Stub_CommentatorGetInstanceInfo },
    { "CommentatorGetMapInfo",                   &Script_Stub_CommentatorGetMapInfo },
    { "CommentatorGetMode",                      &Script_Stub_CommentatorGetMode },
    { "CommentatorGetNumMaps",                   &Script_Stub_CommentatorGetNumMaps },
    { "CommentatorGetNumPlayers",                &Script_Stub_CommentatorGetNumPlayers },
    { "CommentatorGetPlayerInfo",                &Script_Stub_CommentatorGetPlayerInfo },
    { "CommentatorGetSkirmishMode",              &Script_Stub_CommentatorGetSkirmishMode },
    { "CommentatorGetSkirmishQueueCount",        &Script_Stub_CommentatorGetSkirmishQueueCount },
    { "CommentatorGetSkirmishQueuePlayerInfo",   &Script_Stub_CommentatorGetSkirmishQueuePlayerInfo },
    { "CommentatorLookatPlayer",                 &Script_Stub_CommentatorLookatPlayer },
    { "CommentatorRemovePlayer",                 &Script_Stub_CommentatorRemovePlayer },
    { "CommentatorRequestSkirmishMode",          &Script_Stub_CommentatorRequestSkirmishMode },
    { "CommentatorRequestSkirmishQueueData",     &Script_Stub_CommentatorRequestSkirmishQueueData },
    { "CommentatorSetBattlemaster",              &Script_Stub_CommentatorSetBattlemaster },
    { "CommentatorSetCamera",                    &Script_Stub_CommentatorSetCamera },
    { "CommentatorSetCameraCollision",           &Script_Stub_CommentatorSetCameraCollision },
    { "CommentatorSetMapAndInstanceIndex",       &Script_Stub_CommentatorSetMapAndInstanceIndex },
    { "CommentatorSetMode",                      &Script_Stub_CommentatorSetMode },
    { "CommentatorSetMoveSpeed",                 &Script_Stub_CommentatorSetMoveSpeed },
    { "CommentatorSetPlayerIndex",               &Script_Stub_CommentatorSetPlayerIndex },
    { "CommentatorSetSkirmishMatchmakingMode",   &Script_Stub_CommentatorSetSkirmishMatchmakingMode },
    { "CommentatorSetTargetHeightOffset",        &Script_Stub_CommentatorSetTargetHeightOffset },
    { "CommentatorStartInstance",                &Script_Stub_CommentatorStartInstance },
    { "CommentatorStartSkirmishMatch",           &Script_Stub_CommentatorStartSkirmishMatch },
    { "CommentatorToggleMode",                   &Script_Stub_CommentatorToggleMode },
    { "CommentatorUpdateMapInfo",                &Script_Stub_CommentatorUpdateMapInfo },
    { "CommentatorUpdatePlayerInfo",             &Script_Stub_CommentatorUpdatePlayerInfo },
    { "CommentatorZoomIn",                       &Script_Stub_CommentatorZoomIn },
    { "CommentatorZoomOut",                      &Script_Stub_CommentatorZoomOut },
    { "ComplainChat",                            &Script_Stub_ComplainChat },
    { "ComplainInboxItem",                       &Script_Stub_ComplainInboxItem },
    { "CompleteLFGRoleCheck",                    &Script_Stub_CompleteLFGRoleCheck },
    { "CompleteQuest",                           &Script_Stub_CompleteQuest },
    { "ContainerRefundItemPurchase",             &Script_Stub_ContainerRefundItemPurchase },
    { "CreateMacro",                             &Script_Stub_CreateMacro },
    { "CreateMiniWorldMapArrowFrame",            &Script_Stub_CreateMiniWorldMapArrowFrame },
    { "DelIgnore",                               &Script_Stub_DelIgnore },
    { "DelMute",                                 &Script_Stub_DelMute },
    { "DeleteEquipmentSet",                      &Script_Stub_DeleteEquipmentSet },
    { "DeleteInboxItem",                         &Script_Stub_DeleteInboxItem },
    { "DeleteMacro",                             &Script_Stub_DeleteMacro },
    { "DepositGuildBankMoney",                   &Script_Stub_DepositGuildBankMoney },
    { "DetectWowMouse",                          &Script_Stub_DetectWowMouse },
    { "DisableSpellAutocast",                    &Script_Stub_DisableSpellAutocast },
    { "DismissCompanion",                        &Script_Stub_DismissCompanion },
    { "DisplayChannelVoiceOff",                  &Script_Stub_DisplayChannelVoiceOff },
    { "DisplayChannelVoiceOn",                   &Script_Stub_DisplayChannelVoiceOn },
    { "DoEmote",                                 &Script_Stub_DoEmote },
    { "DoTradeSkill",                            &Script_Stub_DoTradeSkill },
    { "DungeonUsesTerrainMap",                   &Script_Stub_DungeonUsesTerrainMap },
    { "EditMacro",                               &Script_Stub_EditMacro },
    { "EnableSpellAutocast",                     &Script_Stub_EnableSpellAutocast },
    { "EnumerateServerChannels",                 &Script_Stub_EnumerateServerChannels },
    { "EquipmentManagerClearIgnoredSlotsForSave", &Script_Stub_EquipmentManagerClearIgnoredSlotsForSave },
    { "EquipmentManagerIgnoreSlotForSave",       &Script_Stub_EquipmentManagerIgnoreSlotForSave },
    { "EquipmentManagerIsSlotIgnoredForSave",    &Script_Stub_EquipmentManagerIsSlotIgnoredForSave },
    { "EquipmentManagerUnignoreSlotForSave",     &Script_Stub_EquipmentManagerUnignoreSlotForSave },
    { "EquipmentSetContainsLockedItems",         &Script_Stub_EquipmentSetContainsLockedItems },
    { "ExpandAllFactionHeaders",                 &Script_Stub_ExpandAllFactionHeaders },
    { "ExpandChannelHeader",                     &Script_Stub_ExpandChannelHeader },
    { "ExpandCurrencyList",                      &Script_Stub_ExpandCurrencyList },
    { "ExpandFactionHeader",                     &Script_Stub_ExpandFactionHeader },
    { "ExpandQuestHeader",                       &Script_Stub_ExpandQuestHeader },
    { "ExpandSkillHeader",                       &Script_Stub_ExpandSkillHeader },
    { "ExpandTradeSkillSubClass",                &Script_Stub_ExpandTradeSkillSubClass },
    { "ExpandTrainerSkillLine",                  &Script_Stub_ExpandTrainerSkillLine },
    { "FactionToggleAtWar",                      &Script_Stub_FactionToggleAtWar },
    { "FindSpellBookSlotByID",                   &Script_Stub_FindSpellBookSlotByID },
    { "FlagTutorial",                            &Script_Stub_FlagTutorial },
    { "FlipCameraYaw",                           &Script_Stub_FlipCameraYaw },
    { "ForceGossip",                             &Script_Stub_ForceGossip },
    { "GetAbandonQuestItems",                    &Script_Stub_GetAbandonQuestItems },
    { "GetAbandonQuestName",                     &Script_Stub_GetAbandonQuestName },
    { "GetAchievementCategory",                  &Script_Stub_GetAchievementCategory },
    { "GetAchievementComparisonInfo",            &Script_Stub_GetAchievementComparisonInfo },
    { "GetAchievementCriteriaInfo",              &Script_Stub_GetAchievementCriteriaInfo },
    { "GetAchievementInfo",                      &Script_Stub_GetAchievementInfo },
    { "GetAchievementInfoFromCriteria",          &Script_Stub_GetAchievementInfoFromCriteria },
    { "GetAchievementLink",                      &Script_Stub_GetAchievementLink },
    { "GetAchievementNumCriteria",               &Script_Stub_GetAchievementNumCriteria },
    { "GetAchievementNumRewards",                &Script_Stub_GetAchievementNumRewards },
    { "GetAchievementReward",                    &Script_Stub_GetAchievementReward },
    { "GetActiveLevel",                          &Script_Stub_GetActiveLevel },
    { "GetActiveTitle",                          &Script_Stub_GetActiveTitle },
    { "GetActiveVoiceChannel",                   &Script_Stub_GetActiveVoiceChannel },
    { "GetArenaTeamGdfInfo",                     &Script_Stub_GetArenaTeamGdfInfo },
    { "GetArenaTeamRosterInfo",                  &Script_Stub_GetArenaTeamRosterInfo },
    { "GetArenaTeamRosterSelection",             &Script_Stub_GetArenaTeamRosterSelection },
    { "GetArenaTeamRosterShowOffline",           &Script_Stub_GetArenaTeamRosterShowOffline },
    { "GetAuctionHouseDepositRate",              &Script_Stub_GetAuctionHouseDepositRate },
    { "GetAuctionInvTypes",                      &Script_Stub_GetAuctionInvTypes },
    { "GetAuctionItemClasses",                   &Script_Stub_GetAuctionItemClasses },
    { "GetAuctionItemInfo",                      &Script_Stub_GetAuctionItemInfo },
    { "GetAuctionItemLink",                      &Script_Stub_GetAuctionItemLink },
    { "GetAuctionItemSubClasses",                &Script_Stub_GetAuctionItemSubClasses },
    { "GetAuctionItemTimeLeft",                  &Script_Stub_GetAuctionItemTimeLeft },
    { "GetAuctionSellItemInfo",                  &Script_Stub_GetAuctionSellItemInfo },
    { "GetAuctionSort",                          &Script_Stub_GetAuctionSort },
    { "GetAutoCompletePresenceID",               &Script_Stub_GetAutoCompletePresenceID },
    { "GetAutoCompleteResults",                  &Script_Stub_GetAutoCompleteResults },
    { "GetAvailableLevel",                       &Script_Stub_GetAvailableLevel },
    { "GetAvailableQuestInfo",                   &Script_Stub_GetAvailableQuestInfo },
    { "GetAvailableRoles",                       &Script_Stub_GetAvailableRoles },
    { "GetAvailableTitle",                       &Script_Stub_GetAvailableTitle },
    { "GetBackpackCurrencyInfo",                 &Script_Stub_GetBackpackCurrencyInfo },
    { "GetBankSlotCost",                         &Script_Stub_GetBankSlotCost },
    { "GetBarberShopStyleInfo",                  &Script_Stub_GetBarberShopStyleInfo },
    { "GetBarberShopTotalCost",                  &Script_Stub_GetBarberShopTotalCost },
    { "GetBidderAuctionItems",                   &Script_Stub_GetBidderAuctionItems },
    { "GetBuybackItemInfo",                      &Script_Stub_GetBuybackItemInfo },
    { "GetBuybackItemLink",                      &Script_Stub_GetBuybackItemLink },
    { "GetCategoryInfo",                         &Script_Stub_GetCategoryInfo },
    { "GetCategoryList",                         &Script_Stub_GetCategoryList },
    { "GetCategoryNumAchievements",              &Script_Stub_GetCategoryNumAchievements },
    { "GetChannelList",                          &Script_Stub_GetChannelList },
    { "GetChannelName",                          &Script_Stub_GetChannelName },
    { "GetChannelRosterInfo",                    &Script_Stub_GetChannelRosterInfo },
    { "GetChatWindowChannels",                   &Script_Stub_GetChatWindowChannels },
    { "GetChatWindowMessages",                   &Script_Stub_GetChatWindowMessages },
    { "GetChatWindowSavedDimensions",            &Script_Stub_GetChatWindowSavedDimensions },
    { "GetChatWindowSavedPosition",              &Script_Stub_GetChatWindowSavedPosition },
    { "GetCompanionCooldown",                    &Script_Stub_GetCompanionCooldown },
    { "GetComparisonAchievementPoints",          &Script_Stub_GetComparisonAchievementPoints },
    { "GetComparisonCategoryNumAchievements",    &Script_Stub_GetComparisonCategoryNumAchievements },
    { "GetComparisonStatistic",                  &Script_Stub_GetComparisonStatistic },
    { "GetContainerItemCooldown",                &Script_Stub_GetContainerItemCooldown },
    { "GetContainerItemGems",                    &Script_Stub_GetContainerItemGems },
    { "GetContainerItemPurchaseInfo",            &Script_Stub_GetContainerItemPurchaseInfo },
    { "GetContainerItemPurchaseItem",            &Script_Stub_GetContainerItemPurchaseItem },
    { "GetContainerItemQuestInfo",               &Script_Stub_GetContainerItemQuestInfo },
    { "GetCorpseMapPosition",                    &Script_Stub_GetCorpseMapPosition },
    { "GetCurrencyListInfo",                     &Script_Stub_GetCurrencyListInfo },
    { "GetCurrencyListSize",                     &Script_Stub_GetCurrencyListSize },
    { "GetCurrentGuildBankTab",                  &Script_Stub_GetCurrentGuildBankTab },
    { "GetCurrentMapAreaID",                     &Script_Stub_GetCurrentMapAreaID },
    { "GetCurrentMapZone",                       &Script_Stub_GetCurrentMapZone },
    { "GetDailyQuestsCompleted",                 &Script_Stub_GetDailyQuestsCompleted },
    { "GetDeathReleasePosition",                 &Script_Stub_GetDeathReleasePosition },
    { "GetDebugZoneMap",                         &Script_Stub_GetDebugZoneMap },
    { "GetDefaultLanguage",                      &Script_Stub_GetDefaultLanguage },
    { "GetEquipmentSetInfo",                     &Script_Stub_GetEquipmentSetInfo },
    { "GetEquipmentSetInfoByName",               &Script_Stub_GetEquipmentSetInfoByName },
    { "GetEquipmentSetItemIDs",                  &Script_Stub_GetEquipmentSetItemIDs },
    { "GetEquipmentSetLocations",                &Script_Stub_GetEquipmentSetLocations },
    { "GetExistingSocketInfo",                   &Script_Stub_GetExistingSocketInfo },
    { "GetExistingSocketLink",                   &Script_Stub_GetExistingSocketLink },
    { "GetFactionInfo",                          &Script_Stub_GetFactionInfo },
    { "GetFactionInfoByID",                      &Script_Stub_GetFactionInfoByID },
    { "GetFirstTradeSkill",                      &Script_Stub_GetFirstTradeSkill },
    { "GetFriendInfo",                           &Script_Stub_GetFriendInfo },
    { "GetGlyphLink",                            &Script_Stub_GetGlyphLink },
    { "GetGlyphSocketInfo",                      &Script_Stub_GetGlyphSocketInfo },
    { "GetGossipActiveQuests",                   &Script_Stub_GetGossipActiveQuests },
    { "GetGossipAvailableQuests",                &Script_Stub_GetGossipAvailableQuests },
    { "GetGossipOptions",                        &Script_Stub_GetGossipOptions },
    { "GetGossipText",                           &Script_Stub_GetGossipText },
    { "GetGreetingText",                         &Script_Stub_GetGreetingText },
    { "GetGuildBankItemInfo",                    &Script_Stub_GetGuildBankItemInfo },
    { "GetGuildBankItemLink",                    &Script_Stub_GetGuildBankItemLink },
    { "GetGuildBankMoney",                       &Script_Stub_GetGuildBankMoney },
    { "GetGuildBankMoneyTransaction",            &Script_Stub_GetGuildBankMoneyTransaction },
    { "GetGuildBankTabCost",                     &Script_Stub_GetGuildBankTabCost },
    { "GetGuildBankTabInfo",                     &Script_Stub_GetGuildBankTabInfo },
    { "GetGuildBankTabPermissions",              &Script_Stub_GetGuildBankTabPermissions },
    { "GetGuildBankText",                        &Script_Stub_GetGuildBankText },
    { "GetGuildBankTransaction",                 &Script_Stub_GetGuildBankTransaction },
    { "GetGuildBankWithdrawLimit",               &Script_Stub_GetGuildBankWithdrawLimit },
    { "GetGuildBankWithdrawMoney",               &Script_Stub_GetGuildBankWithdrawMoney },
    { "GetGuildCharterCost",                     &Script_Stub_GetGuildCharterCost },
    { "GetGuildEventInfo",                       &Script_Stub_GetGuildEventInfo },
    { "GetGuildInfoText",                        &Script_Stub_GetGuildInfoText },
    { "GetGuildRosterInfo",                      &Script_Stub_GetGuildRosterInfo },
    { "GetGuildRosterLastOnline",                &Script_Stub_GetGuildRosterLastOnline },
    { "GetGuildRosterSelection",                 &Script_Stub_GetGuildRosterSelection },
    { "GetGuildRosterShowOffline",               &Script_Stub_GetGuildRosterShowOffline },
    { "GetGuildTabardFileNames",                 &Script_Stub_GetGuildTabardFileNames },
    { "GetIgnoreName",                           &Script_Stub_GetIgnoreName },
    { "GetInboxHeaderInfo",                      &Script_Stub_GetInboxHeaderInfo },
    { "GetInboxInvoiceInfo",                     &Script_Stub_GetInboxInvoiceInfo },
    { "GetInboxItem",                            &Script_Stub_GetInboxItem },
    { "GetInboxItemLink",                        &Script_Stub_GetInboxItemLink },
    { "GetInboxNumItems",                        &Script_Stub_GetInboxNumItems },
    { "GetInboxText",                            &Script_Stub_GetInboxText },
    { "GetLFDChoiceCollapseState",               &Script_Stub_GetLFDChoiceCollapseState },
    { "GetLFDChoiceEnabledState",                &Script_Stub_GetLFDChoiceEnabledState },
    { "GetLFDChoiceInfo",                        &Script_Stub_GetLFDChoiceInfo },
    { "GetLFDChoiceLockedState",                 &Script_Stub_GetLFDChoiceLockedState },
    { "GetLFDChoiceOrder",                       &Script_Stub_GetLFDChoiceOrder },
    { "GetLFDLockInfo",                          &Script_Stub_GetLFDLockInfo },
    { "GetLFDLockPlayerCount",                   &Script_Stub_GetLFDLockPlayerCount },
    { "GetLFGBootProposal",                      &Script_Stub_GetLFGBootProposal },
    { "GetLFGCompletionReward",                  &Script_Stub_GetLFGCompletionReward },
    { "GetLFGCompletionRewardItem",              &Script_Stub_GetLFGCompletionRewardItem },
    { "GetLFGDungeonInfo",                       &Script_Stub_GetLFGDungeonInfo },
    { "GetLFGDungeonRewardInfo",                 &Script_Stub_GetLFGDungeonRewardInfo },
    { "GetLFGDungeonRewardLink",                 &Script_Stub_GetLFGDungeonRewardLink },
    { "GetLFGDungeonRewards",                    &Script_Stub_GetLFGDungeonRewards },
    { "GetLFGInfoLocal",                         &Script_Stub_GetLFGInfoLocal },
    { "GetLFGProposalEncounter",                 &Script_Stub_GetLFGProposalEncounter },
    { "GetLFGProposalMember",                    &Script_Stub_GetLFGProposalMember },
    { "GetLFGQueueStats",                        &Script_Stub_GetLFGQueueStats },
    { "GetLFGRandomCooldownExpiration",          &Script_Stub_GetLFGRandomCooldownExpiration },
    { "GetLFGRandomDungeonInfo",                 &Script_Stub_GetLFGRandomDungeonInfo },
    { "GetLFGRoleUpdateMember",                  &Script_Stub_GetLFGRoleUpdateMember },
    { "GetLFGRoleUpdateSlot",                    &Script_Stub_GetLFGRoleUpdateSlot },
    { "GetLFGTypes",                             &Script_Stub_GetLFGTypes },
    { "GetLFRChoiceOrder",                       &Script_Stub_GetLFRChoiceOrder },
    { "GetLanguageByIndex",                      &Script_Stub_GetLanguageByIndex },
    { "GetLastQueueStatusIndex",                 &Script_Stub_GetLastQueueStatusIndex },
    { "GetLatestCompletedAchievements",          &Script_Stub_GetLatestCompletedAchievements },
    { "GetLatestCompletedComparisonAchievements", &Script_Stub_GetLatestCompletedComparisonAchievements },
    { "GetLatestThreeSenders",                   &Script_Stub_GetLatestThreeSenders },
    { "GetLatestUpdatedComparisonStats",         &Script_Stub_GetLatestUpdatedComparisonStats },
    { "GetLatestUpdatedStats",                   &Script_Stub_GetLatestUpdatedStats },
    { "GetLootRollItemInfo",                     &Script_Stub_GetLootRollItemInfo },
    { "GetLootRollItemLink",                     &Script_Stub_GetLootRollItemLink },
    { "GetLootRollTimeLeft",                     &Script_Stub_GetLootRollTimeLeft },
    { "GetLootSlotInfo",                         &Script_Stub_GetLootSlotInfo },
    { "GetLootSlotLink",                         &Script_Stub_GetLootSlotLink },
    { "GetMacroBody",                            &Script_Stub_GetMacroBody },
    { "GetMacroIconInfo",                        &Script_Stub_GetMacroIconInfo },
    { "GetMacroIndexByName",                     &Script_Stub_GetMacroIndexByName },
    { "GetMacroInfo",                            &Script_Stub_GetMacroInfo },
    { "GetMacroItem",                            &Script_Stub_GetMacroItem },
    { "GetMacroItemIconInfo",                    &Script_Stub_GetMacroItemIconInfo },
    { "GetMacroSpell",                           &Script_Stub_GetMacroSpell },
    { "GetMapContinents",                        &Script_Stub_GetMapContinents },
    { "GetMapDebugObjectInfo",                   &Script_Stub_GetMapDebugObjectInfo },
    { "GetMapLandmarkInfo",                      &Script_Stub_GetMapLandmarkInfo },
    { "GetMapOverlayInfo",                       &Script_Stub_GetMapOverlayInfo },
    { "GetMapZones",                             &Script_Stub_GetMapZones },
    { "GetMaxArenaCurrency",                     &Script_Stub_GetMaxArenaCurrency },
    { "GetMaxDailyQuests",                       &Script_Stub_GetMaxDailyQuests },
    { "GetMerchantItemCostInfo",                 &Script_Stub_GetMerchantItemCostInfo },
    { "GetMerchantItemCostItem",                 &Script_Stub_GetMerchantItemCostItem },
    { "GetMerchantItemInfo",                     &Script_Stub_GetMerchantItemInfo },
    { "GetMerchantItemLink",                     &Script_Stub_GetMerchantItemLink },
    { "GetMerchantItemMaxStack",                 &Script_Stub_GetMerchantItemMaxStack },
    { "GetMerchantNumItems",                     &Script_Stub_GetMerchantNumItems },
    { "GetMinigameState",                        &Script_Stub_GetMinigameState },
    { "GetMinigameType",                         &Script_Stub_GetMinigameType },
    { "GetMuteName",                             &Script_Stub_GetMuteName },
    { "GetMuteStatus",                           &Script_Stub_GetMuteStatus },
    { "GetNewSocketInfo",                        &Script_Stub_GetNewSocketInfo },
    { "GetNewSocketLink",                        &Script_Stub_GetNewSocketLink },
    { "GetNextAchievement",                      &Script_Stub_GetNextAchievement },
    { "GetNextCompleatedTutorial",               &Script_Stub_GetNextCompleatedTutorial },
    { "GetNextStableSlotCost",                   &Script_Stub_GetNextStableSlotCost },
    { "GetOwnerAuctionItems",                    &Script_Stub_GetOwnerAuctionItems },
    { "GetPackageInfo",                          &Script_Stub_GetPackageInfo },
    { "GetPartyLFGBackfillInfo",                 &Script_Stub_GetPartyLFGBackfillInfo },
    { "GetPetActionSlotUsable",                  &Script_Stub_GetPetActionSlotUsable },
    { "GetPetActionsUsable",                     &Script_Stub_GetPetActionsUsable },
    { "GetPetExperience",                        &Script_Stub_GetPetExperience },
    { "GetPetFoodTypes",                         &Script_Stub_GetPetFoodTypes },
    { "GetPetHappiness",                         &Script_Stub_GetPetHappiness },
    { "GetPetIcon",                              &Script_Stub_GetPetIcon },
    { "GetPetTalentTree",                        &Script_Stub_GetPetTalentTree },
    { "GetPetTimeRemaining",                     &Script_Stub_GetPetTimeRemaining },
    { "GetPetitionInfo",                         &Script_Stub_GetPetitionInfo },
    { "GetPetitionItemInfo",                     &Script_Stub_GetPetitionItemInfo },
    { "GetPetitionNameInfo",                     &Script_Stub_GetPetitionNameInfo },
    { "GetPlayerMapPosition",                    &Script_Stub_GetPlayerMapPosition },
    { "GetPrevCompleatedTutorial",               &Script_Stub_GetPrevCompleatedTutorial },
    { "GetPreviousAchievement",                  &Script_Stub_GetPreviousAchievement },
    { "GetQuestBackgroundMaterial",              &Script_Stub_GetQuestBackgroundMaterial },
    { "GetQuestGreenRange",                      &Script_Stub_GetQuestGreenRange },
    { "GetQuestIndexForTimer",                   &Script_Stub_GetQuestIndexForTimer },
    { "GetQuestIndexForWatch",                   &Script_Stub_GetQuestIndexForWatch },
    { "GetQuestItemInfo",                        &Script_Stub_GetQuestItemInfo },
    { "GetQuestItemLink",                        &Script_Stub_GetQuestItemLink },
    { "GetQuestLink",                            &Script_Stub_GetQuestLink },
    { "GetQuestLogChoiceInfo",                   &Script_Stub_GetQuestLogChoiceInfo },
    { "GetQuestLogCompletionText",               &Script_Stub_GetQuestLogCompletionText },
    { "GetQuestLogGroupNum",                     &Script_Stub_GetQuestLogGroupNum },
    { "GetQuestLogItemDrop",                     &Script_Stub_GetQuestLogItemDrop },
    { "GetQuestLogItemLink",                     &Script_Stub_GetQuestLogItemLink },
    { "GetQuestLogLeaderBoard",                  &Script_Stub_GetQuestLogLeaderBoard },
    { "GetQuestLogPushable",                     &Script_Stub_GetQuestLogPushable },
    { "GetQuestLogQuestText",                    &Script_Stub_GetQuestLogQuestText },
    { "GetQuestLogRequiredMoney",                &Script_Stub_GetQuestLogRequiredMoney },
    { "GetQuestLogRewardArenaPoints",            &Script_Stub_GetQuestLogRewardArenaPoints },
    { "GetQuestLogRewardFactionInfo",            &Script_Stub_GetQuestLogRewardFactionInfo },
    { "GetQuestLogRewardHonor",                  &Script_Stub_GetQuestLogRewardHonor },
    { "GetQuestLogRewardInfo",                   &Script_Stub_GetQuestLogRewardInfo },
    { "GetQuestLogRewardMoney",                  &Script_Stub_GetQuestLogRewardMoney },
    { "GetQuestLogRewardSpell",                  &Script_Stub_GetQuestLogRewardSpell },
    { "GetQuestLogRewardTalents",                &Script_Stub_GetQuestLogRewardTalents },
    { "GetQuestLogRewardTitle",                  &Script_Stub_GetQuestLogRewardTitle },
    { "GetQuestLogRewardXP",                     &Script_Stub_GetQuestLogRewardXP },
    { "GetQuestLogSpecialItemCooldown",          &Script_Stub_GetQuestLogSpecialItemCooldown },
    { "GetQuestLogSpecialItemInfo",              &Script_Stub_GetQuestLogSpecialItemInfo },
    { "GetQuestLogSpellLink",                    &Script_Stub_GetQuestLogSpellLink },
    { "GetQuestLogTimeLeft",                     &Script_Stub_GetQuestLogTimeLeft },
    { "GetQuestLogTitle",                        &Script_Stub_GetQuestLogTitle },
    { "GetQuestMoneyToGet",                      &Script_Stub_GetQuestMoneyToGet },
    { "GetQuestPOILeaderBoard",                  &Script_Stub_GetQuestPOILeaderBoard },
    { "GetQuestResetTime",                       &Script_Stub_GetQuestResetTime },
    { "GetQuestReward",                          &Script_Stub_GetQuestReward },
    { "GetQuestSortIndex",                       &Script_Stub_GetQuestSortIndex },
    { "GetQuestSpellLink",                       &Script_Stub_GetQuestSpellLink },
    { "GetQuestText",                            &Script_Stub_GetQuestText },
    { "GetQuestWatchIndex",                      &Script_Stub_GetQuestWatchIndex },
    { "GetQuestWorldMapAreaID",                  &Script_Stub_GetQuestWorldMapAreaID },
    { "GetQuestsCompleted",                      &Script_Stub_GetQuestsCompleted },
    { "GetRandomDungeonBestChoice",              &Script_Stub_GetRandomDungeonBestChoice },
    { "GetRewardArenaPoints",                    &Script_Stub_GetRewardArenaPoints },
    { "GetRewardHonor",                          &Script_Stub_GetRewardHonor },
    { "GetRewardMoney",                          &Script_Stub_GetRewardMoney },
    { "GetRewardTalents",                        &Script_Stub_GetRewardTalents },
    { "GetRewardText",                           &Script_Stub_GetRewardText },
    { "GetRewardTitle",                          &Script_Stub_GetRewardTitle },
    { "GetRewardXP",                             &Script_Stub_GetRewardXP },
    { "GetRunningMacro",                         &Script_Stub_GetRunningMacro },
    { "GetRunningMacroButton",                   &Script_Stub_GetRunningMacroButton },
    { "GetSavedInstanceInfo",                    &Script_Stub_GetSavedInstanceInfo },
    { "GetSelectedAuctionItem",                  &Script_Stub_GetSelectedAuctionItem },
    { "GetSelectedFaction",                      &Script_Stub_GetSelectedFaction },
    { "GetSelectedFriend",                       &Script_Stub_GetSelectedFriend },
    { "GetSelectedIgnore",                       &Script_Stub_GetSelectedIgnore },
    { "GetSelectedMute",                         &Script_Stub_GetSelectedMute },
    { "GetSelectedStablePet",                    &Script_Stub_GetSelectedStablePet },
    { "GetSelectedStationeryTexture",            &Script_Stub_GetSelectedStationeryTexture },
    { "GetSendMailCOD",                          &Script_Stub_GetSendMailCOD },
    { "GetSendMailItem",                         &Script_Stub_GetSendMailItem },
    { "GetSendMailItemLink",                     &Script_Stub_GetSendMailItemLink },
    { "GetSendMailMoney",                        &Script_Stub_GetSendMailMoney },
    { "GetShapeshiftForm",                       &Script_Stub_GetShapeshiftForm },
    { "GetShapeshiftFormCooldown",               &Script_Stub_GetShapeshiftFormCooldown },
    { "GetShapeshiftFormInfo",                   &Script_Stub_GetShapeshiftFormInfo },
    { "GetSocketItemBoundTradeable",             &Script_Stub_GetSocketItemBoundTradeable },
    { "GetSocketItemInfo",                       &Script_Stub_GetSocketItemInfo },
    { "GetSocketItemRefundable",                 &Script_Stub_GetSocketItemRefundable },
    { "GetSocketTypes",                          &Script_Stub_GetSocketTypes },
    { "GetSpellCount",                           &Script_Stub_GetSpellCount },
    { "GetStablePetFoodTypes",                   &Script_Stub_GetStablePetFoodTypes },
    { "GetStablePetInfo",                        &Script_Stub_GetStablePetInfo },
    { "GetStationeryInfo",                       &Script_Stub_GetStationeryInfo },
    { "GetStatistic",                            &Script_Stub_GetStatistic },
    { "GetStatisticsCategoryList",               &Script_Stub_GetStatisticsCategoryList },
    { "GetSuggestedGroupNum",                    &Script_Stub_GetSuggestedGroupNum },
    { "GetTabardInfo",                           &Script_Stub_GetTabardInfo },
    { "GetTalentInfo",                           &Script_Stub_GetTalentInfo },
    { "GetTalentLink",                           &Script_Stub_GetTalentLink },
    { "GetTalentPrereqs",                        &Script_Stub_GetTalentPrereqs },
    { "GetTitleText",                            &Script_Stub_GetTitleText },
    { "GetTotalAchievementPoints",               &Script_Stub_GetTotalAchievementPoints },
    { "GetTradeSkillCooldown",                   &Script_Stub_GetTradeSkillCooldown },
    { "GetTradeSkillDescription",                &Script_Stub_GetTradeSkillDescription },
    { "GetTradeSkillIcon",                       &Script_Stub_GetTradeSkillIcon },
    { "GetTradeSkillInfo",                       &Script_Stub_GetTradeSkillInfo },
    { "GetTradeSkillInvSlotFilter",              &Script_Stub_GetTradeSkillInvSlotFilter },
    { "GetTradeSkillInvSlots",                   &Script_Stub_GetTradeSkillInvSlots },
    { "GetTradeSkillItemLevelFilter",            &Script_Stub_GetTradeSkillItemLevelFilter },
    { "GetTradeSkillItemLink",                   &Script_Stub_GetTradeSkillItemLink },
    { "GetTradeSkillItemNameFilter",             &Script_Stub_GetTradeSkillItemNameFilter },
    { "GetTradeSkillLine",                       &Script_Stub_GetTradeSkillLine },
    { "GetTradeSkillListLink",                   &Script_Stub_GetTradeSkillListLink },
    { "GetTradeSkillNumMade",                    &Script_Stub_GetTradeSkillNumMade },
    { "GetTradeSkillNumReagents",                &Script_Stub_GetTradeSkillNumReagents },
    { "GetTradeSkillReagentInfo",                &Script_Stub_GetTradeSkillReagentInfo },
    { "GetTradeSkillReagentItemLink",            &Script_Stub_GetTradeSkillReagentItemLink },
    { "GetTradeSkillRecipeLink",                 &Script_Stub_GetTradeSkillRecipeLink },
    { "GetTradeSkillSelectionIndex",             &Script_Stub_GetTradeSkillSelectionIndex },
    { "GetTradeSkillSubClassFilter",             &Script_Stub_GetTradeSkillSubClassFilter },
    { "GetTradeSkillSubClasses",                 &Script_Stub_GetTradeSkillSubClasses },
    { "GetTradeSkillTools",                      &Script_Stub_GetTradeSkillTools },
    { "GetTradeskillRepeatCount",                &Script_Stub_GetTradeskillRepeatCount },
    { "GetTrainerGreetingText",                  &Script_Stub_GetTrainerGreetingText },
    { "GetTrainerSelectionIndex",                &Script_Stub_GetTrainerSelectionIndex },
    { "GetTrainerServiceAbilityReq",             &Script_Stub_GetTrainerServiceAbilityReq },
    { "GetTrainerServiceCost",                   &Script_Stub_GetTrainerServiceCost },
    { "GetTrainerServiceDescription",            &Script_Stub_GetTrainerServiceDescription },
    { "GetTrainerServiceIcon",                   &Script_Stub_GetTrainerServiceIcon },
    { "GetTrainerServiceInfo",                   &Script_Stub_GetTrainerServiceInfo },
    { "GetTrainerServiceItemLink",               &Script_Stub_GetTrainerServiceItemLink },
    { "GetTrainerServiceLevelReq",               &Script_Stub_GetTrainerServiceLevelReq },
    { "GetTrainerServiceNumAbilityReq",          &Script_Stub_GetTrainerServiceNumAbilityReq },
    { "GetTrainerServiceSkillLine",              &Script_Stub_GetTrainerServiceSkillLine },
    { "GetTrainerServiceSkillReq",               &Script_Stub_GetTrainerServiceSkillReq },
    { "GetTrainerServiceStepIncrease",           &Script_Stub_GetTrainerServiceStepIncrease },
    { "GetTrainerServiceStepReq",                &Script_Stub_GetTrainerServiceStepReq },
    { "GetTrainerServiceTypeFilter",             &Script_Stub_GetTrainerServiceTypeFilter },
    { "GetTrainerSkillLineFilter",               &Script_Stub_GetTrainerSkillLineFilter },
    { "GetTrainerSkillLines",                    &Script_Stub_GetTrainerSkillLines },
    { "GetVoiceSessionMemberInfoBySessionID",    &Script_Stub_GetVoiceSessionMemberInfoBySessionID },
    { "GetWatchedFactionInfo",                   &Script_Stub_GetWatchedFactionInfo },
    { "GetWhoInfo",                              &Script_Stub_GetWhoInfo },
    { "GetWintergraspWaitTime",                  &Script_Stub_GetWintergraspWaitTime },
    { "GetWorldStateUIInfo",                     &Script_Stub_GetWorldStateUIInfo },
    { "GiveMasterLoot",                          &Script_Stub_GiveMasterLoot },
    { "GlyphMatchesSocket",                      &Script_Stub_GlyphMatchesSocket },
    { "GuildControlAddRank",                     &Script_Stub_GuildControlAddRank },
    { "GuildControlGetNumRanks",                 &Script_Stub_GuildControlGetNumRanks },
    { "GuildControlGetRankFlags",                &Script_Stub_GuildControlGetRankFlags },
    { "GuildControlGetRankName",                 &Script_Stub_GuildControlGetRankName },
    { "GuildControlSaveRank",                    &Script_Stub_GuildControlSaveRank },
    { "GuildControlSetRank",                     &Script_Stub_GuildControlSetRank },
    { "GuildControlSetRankFlag",                 &Script_Stub_GuildControlSetRankFlag },
    { "GuildRoster",                             &Script_Stub_GuildRoster },
    { "GuildRosterSetOfficerNote",               &Script_Stub_GuildRosterSetOfficerNote },
    { "GuildRosterSetPublicNote",                &Script_Stub_GuildRosterSetPublicNote },
    { "HideRepairCursor",                        &Script_Stub_HideRepairCursor },
    { "InRepairMode",                            &Script_Stub_InRepairMode },
    { "InboxItemCanDelete",                      &Script_Stub_InboxItemCanDelete },
    { "IsMouselooking",                          &Script_Stub_IsMouselooking },
    { "ItemTextGetCreator",                      &Script_Stub_ItemTextGetCreator },
    { "ItemTextGetItem",                         &Script_Stub_ItemTextGetItem },
    { "ItemTextGetMaterial",                     &Script_Stub_ItemTextGetMaterial },
    { "ItemTextGetPage",                         &Script_Stub_ItemTextGetPage },
    { "ItemTextGetText",                         &Script_Stub_ItemTextGetText },
    { "ItemTextHasNextPage",                     &Script_Stub_ItemTextHasNextPage },
    { "ItemTextNextPage",                        &Script_Stub_ItemTextNextPage },
    { "ItemTextPrevPage",                        &Script_Stub_ItemTextPrevPage },
    { "JoinChannelByName",                       &Script_Stub_JoinChannelByName },
    { "JoinLFG",                                 &Script_Stub_JoinLFG },
    { "JoinPermanentChannel",                    &Script_Stub_JoinPermanentChannel },
    { "JoinTemporaryChannel",                    &Script_Stub_JoinTemporaryChannel },
    { "LFGTeleport",                             &Script_Stub_LFGTeleport },
    { "LearnPreviewTalents",                     &Script_Stub_LearnPreviewTalents },
    { "LearnTalent",                             &Script_Stub_LearnTalent },
    { "LeaveChannelByName",                      &Script_Stub_LeaveChannelByName },
    { "LeaveLFG",                                &Script_Stub_LeaveLFG },
    { "ListChannels",                            &Script_Stub_ListChannels },
    { "LoggingChat",                             &Script_Stub_LoggingChat },
    { "LoggingCombat",                           &Script_Stub_LoggingCombat },
    { "LootSlot",                                &Script_Stub_LootSlot },
    { "LootSlotIsCoin",                          &Script_Stub_LootSlotIsCoin },
    { "LootSlotIsItem",                          &Script_Stub_LootSlotIsItem },
    { "MakeMinigameMove",                        &Script_Stub_MakeMinigameMove },
    { "ManageBackpackTokenFrame",                &Script_Stub_ManageBackpackTokenFrame },
    { "MouselookStart",                          &Script_Stub_MouselookStart },
    { "MouselookStop",                           &Script_Stub_MouselookStop },
    { "NextView",                                &Script_Stub_NextView },
    { "NumTaxiNodes",                            &Script_Stub_NumTaxiNodes },
    { "OfferPetition",                           &Script_Stub_OfferPetition },
    { "OpenCalendar",                            &Script_Stub_OpenCalendar },
    { "OpenTrainer",                             &Script_Stub_OpenTrainer },
    { "PartyLFGStartBackfill",                   &Script_Stub_PartyLFGStartBackfill },
    { "PetAbandon",                              &Script_Stub_PetAbandon },
    { "PetAggressiveMode",                       &Script_Stub_PetAggressiveMode },
    { "PetAttack",                               &Script_Stub_PetAttack },
    { "PetCanBeAbandoned",                       &Script_Stub_PetCanBeAbandoned },
    { "PetCanBeDismissed",                       &Script_Stub_PetCanBeDismissed },
    { "PetCanBeRenamed",                         &Script_Stub_PetCanBeRenamed },
    { "PetDefensiveMode",                        &Script_Stub_PetDefensiveMode },
    { "PetDismiss",                              &Script_Stub_PetDismiss },
    { "PetFollow",                               &Script_Stub_PetFollow },
    { "PetPassiveMode",                          &Script_Stub_PetPassiveMode },
    { "PetRename",                               &Script_Stub_PetRename },
    { "PetStopAttack",                           &Script_Stub_PetStopAttack },
    { "PetWait",                                 &Script_Stub_PetWait },
    { "PitchDownStart",                          &Script_Stub_PitchDownStart },
    { "PitchDownStop",                           &Script_Stub_PitchDownStop },
    { "PitchUpStart",                            &Script_Stub_PitchUpStart },
    { "PitchUpStop",                             &Script_Stub_PitchUpStop },
    { "PlaceAuctionBid",                         &Script_Stub_PlaceAuctionBid },
    { "PlaceGlyphInSocket",                      &Script_Stub_PlaceGlyphInSocket },
    { "PlayDance",                               &Script_Stub_PlayDance },
    { "PositionMiniWorldMapArrowFrame",          &Script_Stub_PositionMiniWorldMapArrowFrame },
    { "PositionWorldMapArrowFrame",              &Script_Stub_PositionWorldMapArrowFrame },
    { "PrevView",                                &Script_Stub_PrevView },
    { "ProcessMapClick",                         &Script_Stub_ProcessMapClick },
    { "ProcessQuestLogRewardFactions",           &Script_Stub_ProcessQuestLogRewardFactions },
    { "PurchaseSlot",                            &Script_Stub_PurchaseSlot },
    { "QuestChooseRewardError",                  &Script_Stub_QuestChooseRewardError },
    { "QuestFlagsPVP",                           &Script_Stub_QuestFlagsPVP },
    { "QuestGetAutoAccept",                      &Script_Stub_QuestGetAutoAccept },
    { "QuestIsDaily",                            &Script_Stub_QuestIsDaily },
    { "QuestIsWeekly",                           &Script_Stub_QuestIsWeekly },
    { "QuestLogPushQuest",                       &Script_Stub_QuestLogPushQuest },
    { "QuestMapUpdateAllQuests",                 &Script_Stub_QuestMapUpdateAllQuests },
    { "QuestPOIGetIconInfo",                     &Script_Stub_QuestPOIGetIconInfo },
    { "QuestPOIGetQuestIDByIndex",               &Script_Stub_QuestPOIGetQuestIDByIndex },
    { "QuestPOIGetQuestIDByVisibleIndex",        &Script_Stub_QuestPOIGetQuestIDByVisibleIndex },
    { "QuestPOIUpdateIcons",                     &Script_Stub_QuestPOIUpdateIcons },
    { "QuestPOIUpdateTexture",                   &Script_Stub_QuestPOIUpdateTexture },
    { "RejectProposal",                          &Script_Stub_RejectProposal },
    { "RenameEquipmentSet",                      &Script_Stub_RenameEquipmentSet },
    { "RenamePetition",                          &Script_Stub_RenamePetition },
    { "RepairAllItems",                          &Script_Stub_RepairAllItems },
    { "RespondMailLockSendItem",                 &Script_Stub_RespondMailLockSendItem },
    { "ReturnInboxItem",                         &Script_Stub_ReturnInboxItem },
    { "RollOnLoot",                              &Script_Stub_RollOnLoot },
    { "RunMacro",                                &Script_Stub_RunMacro },
    { "RunMacroText",                            &Script_Stub_RunMacroText },
    { "SaveEquipmentSet",                        &Script_Stub_SaveEquipmentSet },
    { "SaveView",                                &Script_Stub_SaveView },
    { "SearchLFGGetEncounterResults",            &Script_Stub_SearchLFGGetEncounterResults },
    { "SearchLFGGetJoinedID",                    &Script_Stub_SearchLFGGetJoinedID },
    { "SearchLFGGetNumResults",                  &Script_Stub_SearchLFGGetNumResults },
    { "SearchLFGGetPartyResults",                &Script_Stub_SearchLFGGetPartyResults },
    { "SearchLFGGetResults",                     &Script_Stub_SearchLFGGetResults },
    { "SearchLFGJoin",                           &Script_Stub_SearchLFGJoin },
    { "SearchLFGLeave",                          &Script_Stub_SearchLFGLeave },
    { "SearchLFGSort",                           &Script_Stub_SearchLFGSort },
    { "SecureCmdOptionParse",                    &Script_Stub_SecureCmdOptionParse },
    { "SelectActiveQuest",                       &Script_Stub_SelectActiveQuest },
    { "SelectAvailableQuest",                    &Script_Stub_SelectAvailableQuest },
    { "SelectGossipActiveQuest",                 &Script_Stub_SelectGossipActiveQuest },
    { "SelectGossipAvailableQuest",              &Script_Stub_SelectGossipAvailableQuest },
    { "SelectGossipOption",                      &Script_Stub_SelectGossipOption },
    { "SelectPackage",                           &Script_Stub_SelectPackage },
    { "SelectStationery",                        &Script_Stub_SelectStationery },
    { "SelectTradeSkill",                        &Script_Stub_SelectTradeSkill },
    { "SelectTrainerService",                    &Script_Stub_SelectTrainerService },
    { "ShiftQuestWatches",                       &Script_Stub_ShiftQuestWatches },
    { "ShowBuybackSellCursor",                   &Script_Stub_ShowBuybackSellCursor },
    { "ShowContainerSellCursor",                 &Script_Stub_ShowContainerSellCursor },
    { "ShowFriends",                             &Script_Stub_ShowFriends },
    { "ShowMerchantSellCursor",                  &Script_Stub_ShowMerchantSellCursor },
    { "ShowMiniWorldMapArrowFrame",              &Script_Stub_ShowMiniWorldMapArrowFrame },
    { "ShowQuickButton",                         &Script_Stub_ShowQuickButton },
    { "ShowRepairCursor",                        &Script_Stub_ShowRepairCursor },
    { "ShowWorldMapArrowFrame",                  &Script_Stub_ShowWorldMapArrowFrame },
    { "SignPetition",                            &Script_Stub_SignPetition },
    { "SocketContainerItem",                     &Script_Stub_SocketContainerItem },
    { "SpellCanTargetGlyph",                     &Script_Stub_SpellCanTargetGlyph },
    { "SpellCanTargetItem",                      &Script_Stub_SpellCanTargetItem },
    { "SpellCanTargetUnit",                      &Script_Stub_SpellCanTargetUnit },
    { "SpellHasRange",                           &Script_Stub_SpellHasRange },
    { "SpellIsTargeting",                        &Script_Stub_SpellIsTargeting },
    { "SpellStopCasting",                        &Script_Stub_SpellStopCasting },
    { "SpellStopTargeting",                      &Script_Stub_SpellStopTargeting },
    { "SpellTargetItem",                         &Script_Stub_SpellTargetItem },
    { "SpellTargetUnit",                         &Script_Stub_SpellTargetUnit },
    { "SplitContainerItem",                      &Script_Stub_SplitContainerItem },
    { "SplitGuildBankItem",                      &Script_Stub_SplitGuildBankItem },
    { "StablePet",                               &Script_Stub_StablePet },
    { "StrafeLeftStart",                         &Script_Stub_StrafeLeftStart },
    { "StrafeLeftStop",                          &Script_Stub_StrafeLeftStop },
    { "StrafeRightStart",                        &Script_Stub_StrafeRightStart },
    { "StrafeRightStop",                         &Script_Stub_StrafeRightStop },
    { "SummonRandomCritter",                     &Script_Stub_SummonRandomCritter },
    { "TakeInboxItem",                           &Script_Stub_TakeInboxItem },
    { "TakeInboxMoney",                          &Script_Stub_TakeInboxMoney },
    { "TakeInboxTextItem",                       &Script_Stub_TakeInboxTextItem },
    { "TakeTaxiNode",                            &Script_Stub_TakeTaxiNode },
    { "TaxiGetDestX",                            &Script_Stub_TaxiGetDestX },
    { "TaxiGetDestY",                            &Script_Stub_TaxiGetDestY },
    { "TaxiGetSrcX",                             &Script_Stub_TaxiGetSrcX },
    { "TaxiGetSrcY",                             &Script_Stub_TaxiGetSrcY },
    { "TaxiNodeCost",                            &Script_Stub_TaxiNodeCost },
    { "TaxiNodeGetType",                         &Script_Stub_TaxiNodeGetType },
    { "TaxiNodeName",                            &Script_Stub_TaxiNodeName },
    { "TaxiNodePosition",                        &Script_Stub_TaxiNodePosition },
    { "TaxiNodeSetCurrent",                      &Script_Stub_TaxiNodeSetCurrent },
    { "TeleportToDebugObject",                   &Script_Stub_TeleportToDebugObject },
    { "TradeSkillOnlyShowMakeable",              &Script_Stub_TradeSkillOnlyShowMakeable },
    { "TradeSkillOnlyShowSkillUps",              &Script_Stub_TradeSkillOnlyShowSkillUps },
    { "UnitHasLFGDeserter",                      &Script_Stub_UnitHasLFGDeserter },
    { "UnitHasLFGRandomCooldown",                &Script_Stub_UnitHasLFGRandomCooldown },
    { "UnitIsSilenced",                          &Script_Stub_UnitIsSilenced },
    { "UnitIsTalking",                           &Script_Stub_UnitIsTalking },
    { "UnstablePet",                             &Script_Stub_UnstablePet },
    { "UpdateMapHighlight",                      &Script_Stub_UpdateMapHighlight },
    { "UpdateWorldMapArrowFrames",               &Script_Stub_UpdateWorldMapArrowFrames },
    { "VehicleAimDecrement",                     &Script_Stub_VehicleAimDecrement },
    { "VehicleAimDownStart",                     &Script_Stub_VehicleAimDownStart },
    { "VehicleAimDownStop",                      &Script_Stub_VehicleAimDownStop },
    { "VehicleAimGetAngle",                      &Script_Stub_VehicleAimGetAngle },
    { "VehicleAimGetNormAngle",                  &Script_Stub_VehicleAimGetNormAngle },
    { "VehicleAimGetNormPower",                  &Script_Stub_VehicleAimGetNormPower },
    { "VehicleAimIncrement",                     &Script_Stub_VehicleAimIncrement },
    { "VehicleAimRequestAngle",                  &Script_Stub_VehicleAimRequestAngle },
    { "VehicleAimRequestNormAngle",              &Script_Stub_VehicleAimRequestNormAngle },
    { "VehicleAimSetNormPower",                  &Script_Stub_VehicleAimSetNormPower },
    { "VehicleAimUpStart",                       &Script_Stub_VehicleAimUpStart },
    { "VehicleAimUpStop",                        &Script_Stub_VehicleAimUpStop },
    { "VehicleCameraZoomIn",                     &Script_Stub_VehicleCameraZoomIn },
    { "VehicleCameraZoomOut",                    &Script_Stub_VehicleCameraZoomOut },
    { "VehicleExit",                             &Script_Stub_VehicleExit },
    { "VehicleNextSeat",                         &Script_Stub_VehicleNextSeat },
    { "VehiclePrevSeat",                         &Script_Stub_VehiclePrevSeat },
    { "VoiceEnumerateCaptureDevices",            &Script_Stub_VoiceEnumerateCaptureDevices },
    { "VoiceEnumerateOutputDevices",             &Script_Stub_VoiceEnumerateOutputDevices },
    { "VoiceGetCurrentCaptureDevice",            &Script_Stub_VoiceGetCurrentCaptureDevice },
    { "VoiceGetCurrentOutputDevice",             &Script_Stub_VoiceGetCurrentOutputDevice },
    { "VoiceSelectCaptureDevice",                &Script_Stub_VoiceSelectCaptureDevice },
    { "VoiceSelectOutputDevice",                 &Script_Stub_VoiceSelectOutputDevice },
    { "WithdrawGuildBankMoney",                  &Script_Stub_WithdrawGuildBankMoney },
    { "ZoomOut",                                 &Script_Stub_ZoomOut },
};

} // namespace

void MiscScriptRegisterStubs() {
    for (auto& func : s_stubs) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
