#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include "../core/vi-core.hpp"
#include <string>

struct SmartFrameData {
    obs_source_t *self = nullptr;
    obs_weak_source_t *target_weak = nullptr;
    std::string target_name;

    gs_effect_t *effect = nullptr;
    gs_texrender_t *texrender = nullptr;

    bool disable_bg = false;
    float bg_opacity = 0.88f;
    uint32_t accent_color = 0xFF9B5CEF; // ABGR for #EF5C9B
    float glow_strength = 0.55f;
    float corner_radius = 12.0f;
    float border_thickness = 2.0f;
    bool react_to_events = true;
    uint32_t width = 640;
    uint32_t height = 360;
};

static const char *smart_frame_get_name(void *) { return "ktb — Smart Frame"; }

static void smart_frame_update(void *data, obs_data_t *settings)
{
    auto *ctx = static_cast<SmartFrameData *>(data);
    const char *new_target = obs_data_get_string(settings, "target_source");
    ctx->disable_bg = obs_data_get_bool(settings, "disable_bg");
    ctx->bg_opacity = static_cast<float>(obs_data_get_double(settings, "bg_opacity"));
    ctx->accent_color = static_cast<uint32_t>(obs_data_get_int(settings, "accent_color"));
    ctx->glow_strength = static_cast<float>(obs_data_get_double(settings, "glow_strength"));
    ctx->corner_radius = static_cast<float>(obs_data_get_double(settings, "corner_radius"));
    ctx->border_thickness = static_cast<float>(obs_data_get_double(settings, "border_thickness"));
    ctx->react_to_events = obs_data_get_bool(settings, "react_to_events");
    ctx->width = static_cast<uint32_t>(obs_data_get_int(settings, "frame_width"));
    ctx->height = static_cast<uint32_t>(obs_data_get_int(settings, "frame_height"));

    std::string next_name = new_target ? new_target : "";
    if (ctx->target_name != next_name) {
        if (ctx->target_weak) {
            obs_weak_source_release(ctx->target_weak);
            ctx->target_weak = nullptr;
        }
        ctx->target_name = next_name;
        if (!ctx->target_name.empty()) {
            obs_source_t *src = obs_get_source_by_name(ctx->target_name.c_str());
            if (src) {
                ctx->target_weak = obs_source_get_weak_source(src);
                obs_source_release(src);
            }
        }
    }
}

static void *smart_frame_create(obs_data_t *settings, obs_source_t *source)
{
    auto *ctx = new SmartFrameData();
    ctx->self = source;

    obs_enter_graphics();
    ctx->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    char *shader_path = obs_module_file("shaders/smart-frame.effect");
    if (shader_path) {
        ctx->effect = gs_effect_create_from_file(shader_path, nullptr);
        bfree(shader_path);
    }
    obs_leave_graphics();

    smart_frame_update(ctx, settings);
    return ctx;
}

static void smart_frame_destroy(void *data)
{
    auto *ctx = static_cast<SmartFrameData *>(data);
    if (ctx->target_weak) obs_weak_source_release(ctx->target_weak);
    obs_enter_graphics();
    if (ctx->effect) gs_effect_destroy(ctx->effect);
    if (ctx->texrender) gs_texrender_destroy(ctx->texrender);
    obs_leave_graphics();
    delete ctx;
}

static uint32_t smart_frame_width(void *data) { return static_cast<SmartFrameData *>(data)->width; }
static uint32_t smart_frame_height(void *data) { return static_cast<SmartFrameData *>(data)->height; }

static void smart_frame_render(void *data, gs_effect_t *)
{
    auto *ctx = static_cast<SmartFrameData *>(data);
    gs_texture_t *src_tex = nullptr;

    if (ctx->target_weak && ctx->texrender) {
        obs_source_t *target = obs_weak_source_get_source(ctx->target_weak);
        if (target) {
            uint32_t tw = obs_source_get_width(target);
            uint32_t th = obs_source_get_height(target);
            if (tw > 0 && th > 0) {
                gs_texrender_reset(ctx->texrender);
                if (gs_texrender_begin(ctx->texrender, tw, th)) {
                    vec4 clear_col;
                    vec4_zero(&clear_col);
                    gs_clear(GS_CLEAR_COLOR, &clear_col, 0.0f, 0);
                    gs_ortho(0.0f, static_cast<float>(tw), 0.0f, static_cast<float>(th), -100.0f, 100.0f);
                    obs_source_video_render(target);
                    gs_texrender_end(ctx->texrender);
                    src_tex = gs_texrender_get_texture(ctx->texrender);
                }
            }
            obs_source_release(target);
        }
    }

    vec4 accent;
    vec4_from_rgba(&accent, ctx->accent_color);
    accent.w = 0.92f;

    float glow = ctx->glow_strength;
    if (ctx->react_to_events) {
        float flash = 0.0f;
        QColor fc = VICore::instance()->getCurrentFlashColor(flash);
        if (flash > 0.01f) {
            accent.x = static_cast<float>(fc.redF());
            accent.y = static_cast<float>(fc.greenF());
            accent.z = static_cast<float>(fc.blueF());
            accent.w = 0.7f + 0.3f * flash;
            glow = std::min(1.5f, glow + flash * 0.65f);
        }
    }

    vec4 bg;
    vec4_set(&bg, 0.063f, 0.067f, 0.086f, ctx->disable_bg ? 0.0f : ctx->bg_opacity);

    if (ctx->effect) {
        vec2 res;
        vec2_set(&res, static_cast<float>(ctx->width), static_cast<float>(ctx->height));

        gs_effect_set_texture(gs_effect_get_param_by_name(ctx->effect, "image"), src_tex);
        gs_effect_set_vec2(gs_effect_get_param_by_name(ctx->effect, "resolution"), &res);
        gs_effect_set_float(gs_effect_get_param_by_name(ctx->effect, "corner_radius"), ctx->corner_radius);
        gs_effect_set_float(gs_effect_get_param_by_name(ctx->effect, "border_thickness"), ctx->border_thickness);
        gs_effect_set_vec4(gs_effect_get_param_by_name(ctx->effect, "border_color"), &accent);
        gs_effect_set_vec4(gs_effect_get_param_by_name(ctx->effect, "bg_color"), &bg);
        gs_effect_set_float(gs_effect_get_param_by_name(ctx->effect, "glow_strength"), glow);
        gs_effect_set_float(gs_effect_get_param_by_name(ctx->effect, "has_image"), src_tex ? 1.0f : 0.0f);

        while (gs_effect_loop(ctx->effect, "Draw")) {
            gs_draw_sprite(src_tex, 0, ctx->width, ctx->height);
        }
    } else if (src_tex) {
        gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        gs_effect_set_texture(gs_effect_get_param_by_name(def, "image"), src_tex);
        while (gs_effect_loop(def, "Draw")) {
            gs_draw_sprite(src_tex, 0, ctx->width, ctx->height);
        }
    }
}

static bool enum_video_sources(void *param, obs_source_t *source)
{
    auto *list = static_cast<obs_property_t *>(param);
    if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) != 0) {
        const char *name = obs_source_get_name(source);
        obs_property_list_add_string(list, name, name);
    }
    return true;
}

static obs_properties_t *smart_frame_properties(void *)
{
    obs_properties_t *props = obs_properties_create();
    obs_property_t *list = obs_properties_add_list(
        props, "target_source", "Встраиваемый источник (вебка / окно)",
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(list, "— Не выбран —", "");
    obs_enum_sources(enum_video_sources, list);

    obs_properties_add_int(props, "frame_width", "Ширина рамки (px)", 160, 1920, 10);
    obs_properties_add_int(props, "frame_height", "Высота рамки (px)", 90, 1080, 10);
    obs_properties_add_float_slider(props, "corner_radius", "Скругление углов (px)", 0.0, 64.0, 1.0);
    obs_properties_add_float_slider(props, "border_thickness", "Толщина рамки (px)", 0.0, 8.0, 0.5);
    obs_properties_add_bool(props, "disable_bg", "Отключить фон");
    obs_properties_add_float_slider(props, "bg_opacity", "Прозрачность фона", 0.0, 1.0, 0.02);
    obs_properties_add_color(props, "accent_color", "Акцентный цвет света");
    obs_properties_add_float_slider(props, "glow_strength", "Интенсивность свечения", 0.0, 1.5, 0.05);
    obs_properties_add_bool(props, "react_to_events", "Подсвечивать рамку при ивентах чата");
    return props;
}

static void smart_frame_defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, "frame_width", 640);
    obs_data_set_default_int(settings, "frame_height", 360);
    obs_data_set_default_double(settings, "corner_radius", 12.0);
    obs_data_set_default_double(settings, "border_thickness", 2.0);
    obs_data_set_default_bool(settings, "disable_bg", false);
    obs_data_set_default_double(settings, "bg_opacity", 0.88);
    obs_data_set_default_int(settings, "accent_color", 0xFF9B5CEF);
    obs_data_set_default_double(settings, "glow_strength", 0.55);
    obs_data_set_default_bool(settings, "react_to_events", true);
}

void register_smart_frame_source()
{
    obs_source_info info = {};
    info.id = "ktb_smart_frame";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name = smart_frame_get_name;
    info.create = smart_frame_create;
    info.destroy = smart_frame_destroy;
    info.update = smart_frame_update;
    info.get_width = smart_frame_width;
    info.get_height = smart_frame_height;
    info.video_render = smart_frame_render;
    info.get_properties = smart_frame_properties;
    info.get_defaults = smart_frame_defaults;
    obs_register_source(&info);
}
