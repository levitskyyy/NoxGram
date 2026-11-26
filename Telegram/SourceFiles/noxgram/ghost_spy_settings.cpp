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
#include "noxgram/lang.h"
#include "noxgram/spy_db.h"
#include "noxgram/spy_media.h"
#include "core/file_utilities.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "styles/style_layers.h"
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

#include <QtCore/QDir>

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

[[nodiscard]] QString MediaLimitText(int index) {
	return (index >= 4)
		? Nox::TrNow(u"lng_nox_deleted_media_unlimited"_q)
		: Ui::FormatSizeText(Spy::MediaLimitBytes(index));
}

void MediaLimitBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(Nox::Tr(u"lng_nox_deleted_media_limit"_q));
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		Nagram::ForDevice().Get(kDeletedMediaLimit));
	for (auto value = 0; value != 5; ++value) {
		box->addRow(object_ptr<Ui::Radiobutton>(
			box,
			group,
			value,
			MediaLimitText(value),
			st::settingsSendType), st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](int value) {
		Expects(Nagram::ForDevice().Set(kDeletedMediaLimit, value));
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void AddMediaLimit(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"noxgram/spy/media-limit"_q,
		.title = Nox::Tr(u"lng_nox_deleted_media_limit"_q),
		.st = &st::settingsButtonNoIcon,
		.label = Nagram::ForDevice().Value(
			kDeletedMediaLimit
		) | rpl::map(MediaLimitText),
		.onClick = [=] {
			if (controller) {
				controller->show(Box(MediaLimitBox));
			}
		},
		.keywords = { u"size"_q, u"limit"_q, u"media"_q },
		.shown = Nagram::ForDevice().Value(kSaveDeletedMedia),
	});
}

void AddStorage(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto usage = std::make_shared<rpl::variable<QString>>(
		Nox::TrNow(u"lng_nox_spy_storage_counting"_q));
	const auto refresh = [=] {
		if (!controller) {
			return;
		}
		Spy::DatabaseFor(&controller->session()).usage([=](Spy::Usage u) {
			*usage = Nox::TrNow(u"lng_nox_spy_storage_value"_q, {
				{ u"deleted"_q, QString::number(u.deleted) },
				{ u"edits"_q, QString::number(u.edits) },
				{ u"size"_q, Ui::FormatSizeText(u.bytes) },
			});
		});
	};
	builder.addSubsectionTitle(Nox::Tr(u"lng_nox_spy_storage"_q));
	const auto info = builder.addButton({
		.id = u"noxgram/spy/storage"_q,
		.title = usage->value(),
		.st = &st::settingsButtonNoIcon,
		.onClick = refresh,
		.keywords = { u"storage"_q, u"size"_q, u"deleted"_q },
	});
	if (info) {
		refresh();
	}
	builder.addButton({
		.id = u"noxgram/spy/open-folder"_q,
		.title = Nox::Tr(u"lng_nox_spy_open_folder"_q),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			if (controller) {
				const auto &database = Spy::DatabaseFor(
					&controller->session());
				QDir().mkpath(database.folder());
				File::ShowInFolder(database.folder() + u"/spy.sqlite"_q);
			}
		},
		.keywords = { u"folder"_q, u"files"_q, u"deleted"_q },
	});
	builder.addButton({
		.id = u"noxgram/spy/clear-all"_q,
		.title = Nox::Tr(u"lng_nox_spy_clear_all"_q),
		.st = &st::settingsAttentionButton,
		.onClick = [=] {
			if (!controller) {
				return;
			}
			controller->show(Ui::MakeConfirmBox({
				.text = Nox::TrNow(u"lng_nox_spy_clear_all_sure"_q),
				.confirmed = [=](Fn<void()> close) {
					Spy::DatabaseFor(&controller->session()).clearAll();
					close();
					controller->showToast(
						Nox::TrNow(u"lng_nox_spy_cleared"_q));
					refresh();
				},
				.confirmText = tr::lng_box_delete(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		},
		.keywords = { u"clear"_q, u"delete"_q, u"deleted"_q, u"storage"_q },
	});
}

const auto kMeta = BuildHelper({
	.id = GhostSpySection::Id(),
	.parentId = Nagram::HomeId(),
	.title = &tr::lng_nox_ghost_spy,
	.icon = &st::menuIconStealth,
}, [](SectionBuilder &builder) {
	builder.addSubsectionTitle(Nox::Tr(u"lng_nox_ghost_title"_q));
	AddToggle(builder, kGhostMode,
		Nox::Tr(u"lng_nox_ghost_mode"_q),
		u"noxgram/ghost/enabled"_q,
		{ u"ghost"_q, u"stealth"_q, u"invisible"_q },
		&st::settingsButton,
		{ &st::menuIconStealth });
	builder.addDividerText(Nox::Tr(u"lng_nox_ghost_mode_about"_q));
	AddToggle(builder, kGhostHideRead,
		Nox::Tr(u"lng_nox_ghost_hide_read"_q),
		u"noxgram/ghost/hide-read"_q,
		{ u"read"_q, u"receipts"_q, u"ghost"_q });
	AddToggle(builder, kGhostHideStories,
		Nox::Tr(u"lng_nox_ghost_hide_stories"_q),
		u"noxgram/ghost/hide-stories"_q,
		{ u"stories"_q, u"views"_q, u"ghost"_q });
	AddToggle(builder, kGhostHideOnline,
		Nox::Tr(u"lng_nox_ghost_hide_online"_q),
		u"noxgram/ghost/hide-online"_q,
		{ u"online"_q, u"last seen"_q, u"ghost"_q });
	AddToggle(builder, kGhostHideTyping,
		Nox::Tr(u"lng_nox_ghost_hide_typing"_q),
		u"noxgram/ghost/hide-typing"_q,
		{ u"typing"_q, u"upload"_q, u"ghost"_q });
	AddToggle(builder, kGhostOfflineAfterOnline,
		Nox::Tr(u"lng_nox_ghost_offline_after_online"_q),
		u"noxgram/ghost/offline-after-online"_q,
		{ u"offline"_q, u"online"_q, u"ghost"_q });
	builder.addDividerText(Nox::Tr(u"lng_nox_ghost_offline_after_online_about"_q));
	AddToggle(builder, kGhostReadOnAction,
		Nox::Tr(u"lng_nox_ghost_read_on_action"_q),
		u"noxgram/ghost/read-on-action"_q,
		{ u"read"_q, u"reply"_q, u"ghost"_q });
	builder.addDividerText(Nox::Tr(u"lng_nox_ghost_read_on_action_about"_q));
	AddToggle(builder, kGhostScheduleSend,
		Nox::Tr(u"lng_nox_ghost_schedule_send"_q),
		u"noxgram/ghost/schedule-send"_q,
		{ u"schedule"_q, u"send"_q, u"ghost"_q });
	builder.addDividerText(Nox::Tr(u"lng_nox_ghost_schedule_send_about"_q));
	AddToggle(builder, kGhostMenuToggle,
		Nox::Tr(u"lng_nox_ghost_menu_toggle"_q),
		u"noxgram/ghost/menu-toggle"_q,
		{ u"menu"_q, u"ghost"_q });

	builder.addSubsectionTitle(Nox::Tr(u"lng_nox_spy_title"_q));
	AddToggle(builder, kSaveDeleted,
		Nox::Tr(u"lng_nox_save_deleted"_q),
		u"noxgram/spy/save-deleted"_q,
		{ u"deleted"_q, u"spy"_q, u"history"_q });
	AddToggle(builder, kSaveEdits,
		Nox::Tr(u"lng_nox_save_edits"_q),
		u"noxgram/spy/save-edits"_q,
		{ u"edited"_q, u"spy"_q, u"history"_q });
	AddToggle(builder, kKeepSelfDestructing,
		Nox::Tr(u"lng_nox_keep_self_destructing"_q),
		u"noxgram/spy/keep-self-destructing"_q,
		{ u"self-destruct"_q, u"timer"_q, u"spy"_q });
	AddToggle(builder, kKeepViewOnce,
		Nox::Tr(u"lng_nox_keep_view_once"_q),
		u"noxgram/spy/keep-view-once"_q,
		{ u"view once"_q, u"self-destruct"_q, u"timer"_q, u"photo"_q });
	builder.addDividerText(Nox::Tr(u"lng_nox_keep_view_once_about"_q));
	AddToggle(builder, kSaveForBots,
		Nox::Tr(u"lng_nox_save_for_bots"_q),
		u"noxgram/spy/save-for-bots"_q,
		{ u"bots"_q, u"spy"_q });
	AddToggle(builder, kSaveDeletedMedia,
		Nox::Tr(u"lng_nox_save_deleted_media"_q),
		u"noxgram/spy/save-media"_q,
		{ u"photo"_q, u"video"_q, u"media"_q, u"spy"_q });
	AddMediaLimit(builder);
	builder.addDividerText(Nox::Tr(u"lng_nox_spy_about"_q));
	AddStorage(builder);
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
	builder.addSubsectionTitle(Nox::Tr(u"lng_nox_ws_title"_q));
	AddToggle(builder, kWsTransport,
		Nox::Tr(u"lng_nox_ws_transport"_q),
		u"noxgram/connection/websocket"_q,
		{ u"websocket"_q, u"web"_q, u"proxy"_q, u"blocking"_q, u"dpi"_q },
		&st::settingsButton,
		{ &st::menuIconNetwork });
	builder.addDividerText(Nox::Tr(u"lng_nox_ws_transport_about"_q));
	AddToggle(builder, kWsDirectFallback,
		Nox::Tr(u"lng_nox_ws_direct_fallback"_q),
		u"noxgram/connection/direct-fallback"_q,
		{ u"websocket"_q, u"direct"_q, u"fallback"_q });
	builder.addDividerText(Nox::Tr(u"lng_nox_ws_direct_fallback_about"_q));
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
