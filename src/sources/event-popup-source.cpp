#include <obs-module.h>
#include <graphics/graphics.h>
#include "../core/vi-core.hpp"
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QDateTime>

struct EventPopupData {
    gs_texture_t *tex = nullptr;
    uint32_t width = 480;
    uint32_t height = 165;
    bool disable_bg = false;
    float bg_opacity = 0.90f;
    qint64 last_paint_ms = 0;
};

static const char *event_popup_get_name(void *) { return "ktb — Event Popup"; }

static void event_popup_update(void *data, obs_data_t *settings)
{
    auto *ctx = static_cast<EventPopupData *>(data);
    ctx->disable_bg = obs_data_get_bool(settings, "disable_bg");
    ctx->bg_opacity = static_cast<float>(obs_data_get_double(settings, "bg_opacity"));
    ctx->last_paint_ms = 0;
}

static void *event_popup_create(obs_data_t *settings, obs_source_t *)
{
    auto *ctx = new EventPopupData();
    event_popup_update(ctx, settings);
    return ctx;
}

static void event_popup_destroy(void *data)
{
    auto *ctx = static_cast<EventPopupData *>(data);
    if (ctx->tex) {
        obs_enter_graphics();
        gs_texture_destroy(ctx->tex);
        obs_leave_graphics();
    }
    delete ctx;
}

static uint32_t event_popup_width(void *data) { return static_cast<EventPopupData *>(data)->width; }
static uint32_t event_popup_height(void *data) { return static_cast<EventPopupData *>(data)->height; }

static void event_popup_render(void *data, gs_effect_t *effect)
{
    auto *ctx = static_cast<EventPopupData *>(data);
    auto ev = VICore::instance()->getActiveEventSnapshot();

    if (!ev.active) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!ctx->tex || (now - ctx->last_paint_ms >= 50)) {
        ctx->last_paint_ms = now;

        QImage img(ctx->width, ctx->height, QImage::Format_RGBA8888);
        img.fill(Qt::transparent);

        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        if (!ctx->disable_bg && ctx->bg_opacity > 0.01f) {
            p.fillRect(img.rect(), QColor(16, 17, 22, static_cast<int>(ctx->bg_opacity * 255)));
            p.setPen(QPen(QColor(255, 255, 255, 28), 1));
            p.drawRect(0, 0, ctx->width - 1, ctx->height - 1);
        }

        p.fillRect(0, 0, 3, ctx->height, ev.rarityColor);

        p.setPen(ev.rarityColor);
        p.setFont(QFont("Consolas", 8, QFont::Bold));
        p.drawText(16, 22, ev.categoryLabel);
        p.drawText(QRect(0, 10, ctx->width - 14, 18), Qt::AlignRight | Qt::AlignVCenter, "[" + ev.rarity + "]");

        p.setPen(QColor(255, 255, 255));
        p.setFont(QFont("Segoe UI", 14, QFont::Bold));
        p.drawText(16, 48, ev.displayName);

        p.setPen(QColor(226, 230, 238));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(QRect(16, 56, ctx->width - 32, 46), Qt::TextWordWrap, ev.message);

        if (ev.replied) {
            p.fillRect(16, 106, ctx->width - 32, 36, QColor(74, 222, 128, 28));
            p.fillRect(16, 106, 2, 36, QColor(74, 222, 128));
            p.setPen(QColor(74, 222, 128));
            p.setFont(QFont("Consolas", 7, QFont::Bold));
            p.drawText(24, 118, "ACKNOWLEDGED // +25 XP");
            p.setPen(QColor(255, 255, 255));
            p.setFont(QFont("Segoe UI", 9));
            p.drawText(24, 134, "«" + ev.replyText + "»");
        } else {
            p.setPen(QColor(139, 146, 160));
            p.setFont(QFont("Consolas", 7));
            p.drawText(16, 142, "AWAITING CHAT RESPONSE...");
        }

        p.fillRect(0, ctx->height - 3, ctx->width, 3, QColor(255, 255, 255, 22));
        p.fillRect(0, ctx->height - 3, static_cast<int>(ctx->width * ev.progress), 3, ev.rarityColor);
        p.end();

        if (!ctx->tex) {
            const uint8_t *bits = img.constBits();
            ctx->tex = gs_texture_create(ctx->width, ctx->height, GS_RGBA, 1, &bits, GS_DYNAMIC);
        } else {
            gs_texture_set_image(ctx->tex, img.constBits(), img.bytesPerLine(), false);
        }
    }

    if (ctx->tex) {
        gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), ctx->tex);
        gs_draw_sprite(ctx->tex, 0, ctx->width, ctx->height);
    }
}

static obs_properties_t *event_popup_properties(void *)
{
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_bool(props, "disable_bg", "Отключить фон");
    obs_properties_add_float_slider(props, "bg_opacity", "Прозрачность фона", 0.0, 1.0, 0.02);
    return props;
}

static void event_popup_defaults(obs_data_t *settings)
{
    obs_data_set_default_bool(settings, "disable_bg", false);
    obs_data_set_default_double(settings, "bg_opacity", 0.90);
}

void register_event_popup_source()
{
    obs_source_info info = {};
    info.id = "ktb_event_popup";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO;
    info.get_name = event_popup_get_name;
    info.create = event_popup_create;
    info.destroy = event_popup_destroy;
    info.update = event_popup_update;
    info.get_width = event_popup_width;
    info.get_height = event_popup_height;
    info.video_render = event_popup_render;
    info.get_properties = event_popup_properties;
    info.get_defaults = event_popup_defaults;
    obs_register_source(&info);
}
