#include <obs-module.h>
#include <graphics/graphics.h>
#include "../core/vi-core.hpp"
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QDateTime>
#include <algorithm>

struct LiveTopData {
    gs_texture_t *tex = nullptr;
    uint32_t width = 320;
    uint32_t height = 250;
    bool disable_bg = false;
    float bg_opacity = 0.88f;
    QColor accent = QColor(239, 92, 155);
    qint64 last_paint_ms = 0;
};

static void drawJawlessSkull(QPainter &p, const QRectF &r, const QColor &col)
{
    p.save();
    p.translate(r.topLeft());
    p.scale(r.width() / 64.0, r.height() / 64.0);
    p.setPen(Qt::NoPen);
    p.setBrush(col);

    QPainterPath cranium;
    cranium.addRoundedRect(10, 8, 44, 34, 18, 18);
    cranium.addRect(18, 36, 28, 13);
    p.drawPath(cranium);

    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.drawEllipse(QPointF(22, 27), 6.0, 6.0);
    p.drawEllipse(QPointF(42, 27), 6.0, 6.0);
    QPolygonF nose;
    nose << QPointF(32, 33) << QPointF(28.5, 40) << QPointF(35.5, 40);
    p.drawPolygon(nose);
    p.drawRect(24, 43, 2.5, 7);
    p.drawRect(30.7, 43, 2.5, 7);
    p.drawRect(37.5, 43, 2.5, 7);
    p.restore();
}

static const char *live_top_get_name(void *) { return "ktb — Live Top"; }

static void live_top_update(void *data, obs_data_t *settings)
{
    auto *ctx = static_cast<LiveTopData *>(data);
    ctx->disable_bg = obs_data_get_bool(settings, "disable_bg");
    ctx->bg_opacity = static_cast<float>(obs_data_get_double(settings, "bg_opacity"));
    uint32_t abgr = static_cast<uint32_t>(obs_data_get_int(settings, "accent_color"));
    ctx->accent = QColor(abgr & 0xFF, (abgr >> 8) & 0xFF, (abgr >> 16) & 0xFF);
    ctx->last_paint_ms = 0;
}

static void *live_top_create(obs_data_t *settings, obs_source_t *)
{
    auto *ctx = new LiveTopData();
    live_top_update(ctx, settings);
    return ctx;
}

static void live_top_destroy(void *data)
{
    auto *ctx = static_cast<LiveTopData *>(data);
    if (ctx->tex) {
        obs_enter_graphics();
        gs_texture_destroy(ctx->tex);
        obs_leave_graphics();
    }
    delete ctx;
}

static uint32_t live_top_width(void *data) { return static_cast<LiveTopData *>(data)->width; }
static uint32_t live_top_height(void *data) { return static_cast<LiveTopData *>(data)->height; }

static void live_top_render(void *data, gs_effect_t *effect)
{
    auto *ctx = static_cast<LiveTopData *>(data);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (!ctx->tex || (now - ctx->last_paint_ms >= 100)) {
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
            p.setPen(QPen(ctx->accent, 2));
            p.drawLine(0, 0, 8, 0); p.drawLine(0, 0, 0, 8);
            p.drawLine(ctx->width - 8, 0, ctx->width, 0); p.drawLine(ctx->width - 1, 0, ctx->width - 1, 8);
        }

        drawJawlessSkull(p, QRectF(12, 10, 22, 22), QColor(242, 244, 248));
        p.setPen(QColor(242, 244, 248));
        p.setFont(QFont("Segoe UI", 10, QFont::Bold));
        p.drawText(40, 21, "KTOTAKOYBEN");

        p.setPen(QColor(139, 146, 160));
        p.setFont(QFont("Consolas", 7));
        p.drawText(40, 32, "VIEWER INTEL // " + VICore::instance()->rankingMode().toUpper());

        p.setPen(QColor(255, 255, 255, 24));
        p.drawLine(10, 40, ctx->width - 10, 40);

        int activeUsers = 0, nextScanSec = 0;
        auto top5 = VICore::instance()->getTop5Snapshot(activeUsers, nextScanSec);

        if (top5.isEmpty()) {
            p.setPen(QColor(139, 146, 160));
            p.setFont(QFont("Consolas", 8));
            p.drawText(QRect(0, 45, ctx->width, 175), Qt::AlignCenter, "AWAITING CHAT SIGNAL...");
        } else {
            int maxScore = std::max(1, top5[0].score);
            for (int i = 0; i < top5.size(); ++i) {
                int y = 48 + i * 34;
                const auto &item = top5[i];

                p.setPen(i == 0 ? ctx->accent : QColor(139, 146, 160));
                p.setFont(QFont("Consolas", 8, QFont::Bold));
                p.drawText(12, y + 14, QString("0%1").arg(i + 1));

                p.setPen(QColor(242, 244, 248));
                p.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
                p.drawText(34, y + 14, item.name.left(18));

                QString xpStr = QString("%1 XP").arg(item.score);
                p.setFont(QFont("Consolas", 8));
                p.drawText(QRect(180, y + 2, ctx->width - 192, 16), Qt::AlignRight | Qt::AlignVCenter, xpStr);

                if (item.showDelta && item.deltaXp > 0) {
                    p.setPen(ctx->accent);
                    p.drawText(QRect(130, y + 2, 70, 16), Qt::AlignRight | Qt::AlignVCenter, QString("+%1").arg(item.deltaXp));
                }

                p.fillRect(12, y + 22, ctx->width - 24, 2, QColor(255, 255, 255, 22));
                int barW = std::max(8, static_cast<int>((ctx->width - 24) * (static_cast<float>(item.score) / maxScore)));
                p.fillRect(12, y + 22, barW, 2, ctx->accent);
            }
        }

        p.setPen(QColor(255, 255, 255, 24));
        p.drawLine(10, ctx->height - 24, ctx->width - 10, ctx->height - 24);
        p.setPen(QColor(139, 146, 160));
        p.setFont(QFont("Consolas", 7));
        p.drawText(12, ctx->height - 8, QString("ACTIVE: %1").arg(activeUsers));
        p.drawText(QRect(0, ctx->height - 22, ctx->width - 12, 20), Qt::AlignRight | Qt::AlignVCenter,
                   QString("SCAN: %1:%2").arg(nextScanSec / 60).arg(nextScanSec % 60, 2, 10, QChar('0')));
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

static obs_properties_t *live_top_properties(void *)
{
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_bool(props, "disable_bg", "Отключить фон (полная прозрачность)");
    obs_properties_add_float_slider(props, "bg_opacity", "Прозрачность фона", 0.0, 1.0, 0.02);
    obs_properties_add_color(props, "accent_color", "Акцентный цвет");
    return props;
}

static void live_top_defaults(obs_data_t *settings)
{
    obs_data_set_default_bool(settings, "disable_bg", false);
    obs_data_set_default_double(settings, "bg_opacity", 0.88);
    obs_data_set_default_int(settings, "accent_color", 0xFF9B5CEF);
}

void register_live_top_source()
{
    obs_source_info info = {};
    info.id = "ktb_live_top";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO;
    info.get_name = live_top_get_name;
    info.create = live_top_create;
    info.destroy = live_top_destroy;
    info.update = live_top_update;
    info.get_width = live_top_width;
    info.get_height = live_top_height;
    info.video_render = live_top_render;
    info.get_properties = live_top_properties;
    info.get_defaults = live_top_defaults;
    obs_register_source(&info);
}
