#include <obs-module.h>
#include <obs-frontend-api.h>
#include "core/vi-core.hpp"
#include "ui/control-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-ktotakoyben-vi", "en-US")

extern void register_smart_frame_source();
extern void register_live_top_source();
extern void register_event_popup_source();

static void on_frontend_event(enum obs_frontend_event event, void *)
{
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        VICore::instance()->initialize();
        auto *dock = new ControlDock();
        obs_frontend_add_dock_by_id("ktb_vi_control_dock", "ktotakoyben Control", dock);
    } else if (event == OBS_FRONTEND_EVENT_EXIT) {
        VICore::instance()->shutdown();
    }
}

bool obs_module_load(void)
{
    register_smart_frame_source();
    register_live_top_source();
    register_event_popup_source();
    obs_frontend_add_event_callback(on_frontend_event, nullptr);
    blog(LOG_INFO, "[ktotakoyben-vi] Loaded successfully");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[ktotakoyben-vi] Unloaded");
}
