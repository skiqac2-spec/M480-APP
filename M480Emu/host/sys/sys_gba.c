/* Game Boy Advance (gpSP) */
#include "host.h"

static void apply(void)
{
    static const char *fixed[] = { "1", "2", "3" };
    core_var("gpsp_drc", "enabled");
    core_var("gpsp_bios", "auto");
    core_var("gpsp_boot_mode", "game");
    core_var("gpsp_sound_rate", "32768");
    core_var("gpsp_color_correction", "disabled");
    core_var("gpsp_frame_mixing", "disabled");
    core_var("gpsp_sprlim", "disabled");
    if (cfg.frameskip == 0)
        core_var("gpsp_frameskip", "disabled");
    else if (cfg.frameskip == 1) {
        core_var("gpsp_frameskip", "auto");
        core_var("gpsp_frameskip_interval", "4");
    } else {
        core_var("gpsp_frameskip", "fixed_interval");
        core_var("gpsp_frameskip_interval", fixed[(cfg.frameskip - 2) % 3]);
    }
}

const struct sysdef sys = {
    .id = "gba", .name = u"게임보이 어드밴스", .game_h = 256, .scale = SC_UNIFORM, .flags = SF_FRAMESKIP,
    .btn_names = { [PB_UP] = u"방향 위", [PB_DOWN] = u"방향 아래", [PB_LEFT] = u"방향 왼쪽", [PB_RIGHT] = u"방향 오른쪽",
                   [PB_CIRCLE] = u"A 버튼", [PB_CROSS] = u"B 버튼", [PB_L1] = u"L 버튼", [PB_R1] = u"R 버튼",
                   [PB_START] = u"START", [PB_SELECT] = u"SELECT" },
    .bar = { PB_L1, PB_SELECT, PB_START, PB_R1, -1 },
    .bar_names = { u"L", u"SEL", u"STA", u"R" },
    .pad_names = { NULL, u"A", u"B", NULL },
    .apply = apply,
};
