/* Game Boy / Game Boy Color (Gambatte) */
#include "host.h"

static void apply(void)
{
    core_var("gambatte_gb_colorization", "auto");
    core_var("gambatte_gb_hwmode", "Auto");
    core_var("gambatte_gb_bootloader", "disabled");
    core_var("gambatte_mix_frames", "disabled");
    core_var("gambatte_gbc_color_correction", "disabled");
    core_var("gambatte_dark_filter_level", "0");
    core_var("gambatte_audio_resampler", "cc");
    core_var("gambatte_up_down_allowed", "disabled");
}

const struct sysdef sys = {
    .id = "gb", .name = u"게임보이 / 컬러", .game_h = 288, .scale = SC_2X, .flags = 0,
    .btn_names = { [PB_UP] = u"방향 위", [PB_DOWN] = u"방향 아래", [PB_LEFT] = u"방향 왼쪽", [PB_RIGHT] = u"방향 오른쪽",
                   [PB_CIRCLE] = u"A 버튼", [PB_CROSS] = u"B 버튼",
                   [PB_START] = u"START", [PB_SELECT] = u"SELECT" },
    .bar = { PB_SELECT, PB_START, -1 },
    .bar_names = { u"SEL", u"STA" },
    .pad_names = { NULL, u"A", u"B", NULL },
    .apply = apply,
};
