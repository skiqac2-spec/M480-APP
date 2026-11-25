/* Famicom / NES (FCEUmm) */
#include "host.h"

static void apply(void)
{
    core_var("fceumm_region", "Auto");
    core_var("fceumm_sndquality", "Low");
    core_var("fceumm_sndrate_hint", "44KHz");
    core_var("fceumm_overscan_h_left", "0");
    core_var("fceumm_overscan_h_right", "0");
    core_var("fceumm_overscan_v_top", "8");
    core_var("fceumm_overscan_v_bottom", "8");
    core_var("fceumm_turbo_enable", "None");
    core_var("fceumm_ntsc_filter", "disabled");
}

const struct sysdef sys = {
    .id = "nes", .name = u"패미컴 (NES)", .game_h = 256, .scale = SC_WIDTH, .flags = SF_FRAMESKIP | SF_HOSTSKIP,
    .btn_names = { [PB_UP] = u"방향 위", [PB_DOWN] = u"방향 아래", [PB_LEFT] = u"방향 왼쪽", [PB_RIGHT] = u"방향 오른쪽",
                   [PB_CIRCLE] = u"A 버튼", [PB_CROSS] = u"B 버튼",
                   [PB_START] = u"START", [PB_SELECT] = u"SELECT" },
    .bar = { PB_SELECT, PB_START, -1 },
    .bar_names = { u"SEL", u"STA" },
    .pad_names = { NULL, u"A", u"B", NULL },
    .apply = apply,
};
