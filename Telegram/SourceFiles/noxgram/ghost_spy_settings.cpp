/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

Ghost mode and spy mode are modelled on the features of AyuGram Desktop
(https://github.com/AyuGram/AyuGramDesktop), licensed under GPLv3.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "noxgram/ghost_spy_settings.h"

#include "noxgram/ghost.h"
#include "noxgram/options.h"
#include "nagram/settings/home.h"
#include "lang/lang_keys.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Nox {
namespace {

using namespace ::Settings;
using namespace ::Settings::Builder;

class GhostSpySection final : public Section<GhostSpySection> {
public:
	GhostSpySection(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		build(content, kBuild);
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return tr::lng_nox_ghost_spy();
	}

	static const SectionBuildMethod kBuild;
};

void AddToggle(
		SectionBuilder &builder,
		const Option<bool> &option,
		rpl::producer<QString> title,
		QString id,
		QStringList keywords,
		const style::SettingsButton *st = nullptr,
		IconDescriptor icon = {}) {
	const auto button = builder.addButton({
		.id = std::move(id),
		.title = std::move(title),
		.st = st ? st : &st::settingsButtonNoIcon,
		.icon = std::move(icon),
		.toggled = Nagram::ForDevice().Value(option),
		.keywords = std::move(keywords),
	});
	if (button) {
		button->toggledChanges(
		) | rpl::on_next([option](bool value) {
			if (option.key == kGhostMode.key) {
				Ghost::SetActive(value);
			} else {
				Expects(Nagram::ForDevice().Set(option, value));
			}
		}, button->lifetime());
	}
}

const auto kMeta = BuildHelper({
	.id = GhostSpySection::Id(),
	.parentId = Nagram::HomeId(),
	.title = &tr::lng_nox_ghost_spy,
	.icon = &st::menuIconStealth,
}, [](SectionBuilder &builder) {
	builder.addSubsectionTitle(tr::lng_nox_ghost_title());
	AddToggle(builder, kGhostMode,
		tr::lng_nox_ghost_mode(),
		u"noxgram/ghost/enabled"_q,
		{ u"ghost"_q, u"stealth"_q, u"invisible"_q },
		&st::settingsButton,
		{ &st::menuIconStealth });
	builder.addDividerText(tr::lng_nox_ghost_mode_about());
	AddToggle(builder, kGhostHideRead,
		tr::lng_nox_ghost_hide_read(),
		u"noxgram/ghost/hide-read"_q,
		{ u"read"_q, u"receipts"_q, u"ghost"_q });
	AddToggle(builder, kGhostHideStories,
		tr::lng_nox_ghost_hide_stories(),
		u"noxgram/ghost/hide-stories"_q,
		{ u"stories"_q, u"views"_q, u"ghost"_q });
	AddToggle(builder, kGhostHideOnline,
		tr::lng_nox_ghost_hide_online(),
		u"noxgram/ghost/hide-online"_q,
		{ u"online"_q, u"last seen"_q, u"ghost"_q });
	AddToggle(builder, kGhostHideTyping,
		tr::lng_nox_ghost_hide_typing(),
		u"noxgram/ghost/hide-typing"_q,
		{ u"typing"_q, u"upload"_q, u"ghost"_q });
	AddToggle(builder, kGhostOfflineAfterOnline,
		tr::lng_nox_ghost_offline_after_online(),
		u"noxgram/ghost/offline-after-online"_q,
		{ u"offline"_q, u"online"_q, u"ghost"_q });
	builder.addDividerText(tr::lng_nox_ghost_offline_after_online_about());
	AddToggle(builder, kGhostReadOnAction,
		tr::lng_nox_ghost_read_on_action(),
		u"noxgram/ghost/read-on-action"_q,
		{ u"read"_q, u"reply"_q, u"ghost"_q });
	builder.addDividerText(tr::lng_nox_ghost_read_on_action_about());
	AddToggle(builder, kGhostScheduleSend,
		tr::lng_nox_ghost_schedule_send(),
		u"noxgram/ghost/schedule-send"_q,
		{ u"schedule"_q, u"send"_q, u"ghost"_q });
	builder.addDividerText(tr::lng_nox_ghost_schedule_send_about());
	AddToggle(builder, kGhostMenuToggle,
		tr::lng_nox_ghost_menu_toggle(),
		u"noxgram/ghost/menu-toggle"_q,
		{ u"menu"_q, u"ghost"_q });

	builder.addSubsectionTitle(tr::lng_nox_spy_title());
	AddToggle(builder, kSaveDeleted,
		tr::lng_nox_save_deleted(),
		u"noxgram/spy/save-deleted"_q,
		{ u"deleted"_q, u"spy"_q, u"history"_q });
	AddToggle(builder, kSaveEdits,
		tr::lng_nox_save_edits(),
		u"noxgram/spy/save-edits"_q,
		{ u"edited"_q, u"spy"_q, u"history"_q });
	AddToggle(builder, kKeepSelfDestructing,
		tr::lng_nox_keep_self_destructing(),
		u"noxgram/spy/keep-self-destructing"_q,
		{ u"self-destruct"_q, u"timer"_q, u"spy"_q });
	AddToggle(builder, kSaveForBots,
		tr::lng_nox_save_for_bots(),
		u"noxgram/spy/save-for-bots"_q,
		{ u"bots"_q, u"spy"_q });
	builder.addDividerText(tr::lng_nox_spy_about());
});

const SectionBuildMethod GhostSpySection::kBuild = kMeta.build;

class ConnectionSection final : public Section<ConnectionSection> {
public:
	ConnectionSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		build(content, kBuild);
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return tr::lng_nox_connection();
	}

	static const SectionBuildMethod kBuild;
};

const auto kConnectionMeta = BuildHelper({
	.id = ConnectionSection::Id(),
	.parentId = Nagram::HomeId(),
	.title = &tr::lng_nox_connection,
	.icon = &st::menuIconNetwork,
}, [](SectionBuilder &builder) {
	builder.addSubsectionTitle(tr::lng_nox_ws_title());
	AddToggle(builder, kWsTransport,
		tr::lng_nox_ws_transport(),
		u"noxgram/connection/websocket"_q,
		{ u"websocket"_q, u"web"_q, u"proxy"_q, u"blocking"_q, u"dpi"_q },
		&st::settingsButton,
		{ &st::menuIconNetwork });
	builder.addDividerText(tr::lng_nox_ws_transport_about());
	AddToggle(builder, kWsDirectFallback,
		tr::lng_nox_ws_direct_fallback(),
		u"noxgram/connection/direct-fallback"_q,
		{ u"websocket"_q, u"direct"_q, u"fallback"_q });
	builder.addDividerText(tr::lng_nox_ws_direct_fallback_about());
});

const SectionBuildMethod ConnectionSection::kBuild = kConnectionMeta.build;

} // namespace

Settings::Type GhostSpyId() {
	return GhostSpySection::Id();
}

Settings::Type ConnectionId() {
	return ConnectionSection::Id();
}

} // namespace Nox
