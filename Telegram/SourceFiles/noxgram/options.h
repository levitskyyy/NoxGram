/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

Ghost mode and spy mode are modelled on the features of AyuGram Desktop
(https://github.com/AyuGram/AyuGramDesktop), licensed under GPLv3.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "nagram/core/options.h"

// NoxGram options live in the Nagram option registry, so they take part in
// the config export and cloud sync. Registry keys must start with "nagram.".
namespace Nox {

using Nagram::Category;
using Nagram::Flag;
using Nagram::Option;
using Nagram::Scope;

inline constexpr auto kRefreshView = static_cast<unsigned>(
	Flag::RefreshMessageView);

// Ghost mode: one master switch, each part can be excluded separately.
inline constexpr auto kGhostMode = Option<bool>{
	"nagram.nox.ghostMode", Scope::Device, false,
	Category::Privacy, "lng_nox_ghost_mode" };
inline constexpr auto kGhostHideRead = Option<bool>{
	"nagram.nox.ghostHideRead", Scope::Device, true,
	Category::Privacy, "lng_nox_ghost_hide_read" };
inline constexpr auto kGhostHideStories = Option<bool>{
	"nagram.nox.ghostHideStories", Scope::Device, true,
	Category::Privacy, "lng_nox_ghost_hide_stories" };
inline constexpr auto kGhostHideOnline = Option<bool>{
	"nagram.nox.ghostHideOnline", Scope::Device, true,
	Category::Privacy, "lng_nox_ghost_hide_online" };
inline constexpr auto kGhostHideTyping = Option<bool>{
	"nagram.nox.ghostHideTyping", Scope::Device, true,
	Category::Privacy, "lng_nox_ghost_hide_typing" };
inline constexpr auto kGhostOfflineAfterOnline = Option<bool>{
	"nagram.nox.ghostOfflineAfterOnline", Scope::Device, true,
	Category::Privacy, "lng_nox_ghost_offline_after_online" };
inline constexpr auto kGhostReadOnAction = Option<bool>{
	"nagram.nox.ghostReadOnAction", Scope::Device, true,
	Category::Privacy, "lng_nox_ghost_read_on_action" };
inline constexpr auto kGhostScheduleSend = Option<bool>{
	"nagram.nox.ghostScheduleSend", Scope::Device, false,
	Category::Privacy, "lng_nox_ghost_schedule_send" };
inline constexpr auto kSendWithoutSound = Option<int>{
	"nagram.nox.sendWithoutSound", Scope::Device, 0,
	Category::Privacy, "lng_nox_send_without_sound", 0u,
	[](const int &value) { return value >= 0 && value <= 2; } };
inline constexpr auto kGhostStoryPrompt = Option<bool>{
	"nagram.nox.ghostStoryPrompt", Scope::Device, true,
	Category::Privacy, "lng_nox_ghost_story_prompt" };
inline constexpr auto kGhostTrayToggle = Option<bool>{
	"nagram.nox.ghostTrayToggle", Scope::Device, true,
	Category::Privacy, "lng_nox_ghost_tray_toggle" };
inline constexpr auto kDemoMenuToggle = Option<bool>{
	"nagram.nox.demoMenuToggle", Scope::Device, false,
	Category::Privacy, "lng_nox_demo_menu_toggle" };
inline constexpr auto kGhostMenuToggle = Option<bool>{
	"nagram.nox.ghostMenuToggle", Scope::Device, true,
	Category::Privacy, "lng_nox_ghost_menu_toggle" };

// Spy mode: keep what others delete or edit.
inline constexpr auto kSaveDeleted = Option<bool>{
	"nagram.nox.saveDeleted", Scope::Device, true,
	Category::Privacy, "lng_nox_save_deleted", kRefreshView };
inline constexpr auto kSaveEdits = Option<bool>{
	"nagram.nox.saveEdits", Scope::Device, true,
	Category::Privacy, "lng_nox_save_edits" };
inline constexpr auto kSaveForBots = Option<bool>{
	"nagram.nox.saveForBots", Scope::Device, false,
	Category::Privacy, "lng_nox_save_for_bots" };
inline constexpr auto kKeepSelfDestructing = Option<bool>{
	"nagram.nox.keepSelfDestructing", Scope::Device, true,
	Category::Privacy, "lng_nox_keep_self_destructing" };
inline constexpr auto kKeepViewOnce = Option<bool>{
	"nagram.nox.keepViewOnce", Scope::Device, true,
	Category::Privacy, "lng_nox_keep_view_once", kRefreshView };
inline constexpr auto kSaveDeletedMedia = Option<bool>{
	"nagram.nox.saveDeletedMedia", Scope::Device, true,
	Category::Privacy, "lng_nox_save_deleted_media" };
inline constexpr auto kDeletedMediaLimit = Option<int>{
	"nagram.nox.deletedMediaLimit", Scope::Device, 2,
	Category::Privacy, "lng_nox_deleted_media_limit", 0u,
	[](const int &value) { return value >= 0 && value <= 4; } };

inline constexpr auto kForwardProtected = Option<bool>{
	"nagram.nox.forwardProtected", Scope::Device, true,
	Category::Privacy, "lng_nox_forward_protected" };

// Connection: MTProto inside a WebSocket to web.telegram.org.
inline constexpr auto kWsTransport = Option<bool>{
	"nagram.nox.wsTransport", Scope::Device, false,
	Category::Network, "lng_nox_ws_transport" };
inline constexpr auto kWsDirectFallback = Option<bool>{
	"nagram.nox.wsDirectFallback", Scope::Device, true,
	Category::Network, "lng_nox_ws_direct_fallback" };

inline void RegisterOptions(Nagram::Registry &registry) {
	Expects(registry.Add(kGhostMode));
	Expects(registry.Add(kGhostHideRead));
	Expects(registry.Add(kGhostHideStories));
	Expects(registry.Add(kGhostHideOnline));
	Expects(registry.Add(kGhostHideTyping));
	Expects(registry.Add(kGhostOfflineAfterOnline));
	Expects(registry.Add(kGhostReadOnAction));
	Expects(registry.Add(kGhostScheduleSend));
	Expects(registry.Add(kGhostMenuToggle));
	Expects(registry.Add(kSendWithoutSound));
	Expects(registry.Add(kGhostStoryPrompt));
	Expects(registry.Add(kGhostTrayToggle));
	Expects(registry.Add(kDemoMenuToggle));
	Expects(registry.Add(kSaveDeleted));
	Expects(registry.Add(kSaveEdits));
	Expects(registry.Add(kSaveForBots));
	Expects(registry.Add(kKeepSelfDestructing));
	Expects(registry.Add(kKeepViewOnce));
	Expects(registry.Add(kSaveDeletedMedia));
	Expects(registry.Add(kDeletedMediaLimit));
	Expects(registry.Add(kForwardProtected));
	Expects(registry.Add(kWsTransport));
	Expects(registry.Add(kWsDirectFallback));
}

} // namespace Nox
