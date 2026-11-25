/* Mega Drive / Genesis, Master System, Game Gear (PicoDrive) */
#include "host.h"

static void apply(void)
{
    core_var("picodrive_input1", "6 button pad");
    core_var("picodrive_input2", "3 button pad");
    core_var("picodrive_region", "Auto");
    core_var("picodrive_renderer", "fast");
    core_var("picodrive_sound_rate", "44100");
    core_var("picodrive_fm_filter", "off");
    core_var("picodrive_audio_filter", "disabled");
    core_var("picodrive_dacnoise", "off");
    core_var("picodrive_ggghost", "off");
    core_var("picodrive_sprlim", "disabled");
    core_var("picodrive_overscan", "disabled");
    core_var("picodrive_frameskip", cfg.frameskip == 0 ? "disabled" : "auto");
}

const struct sysdef sys = {
    .id = "md", .name = u"메가드라이브", .game_h = 256, .scale = SC_WIDTH, .flags = SF_FRAMESKIP,
    .btn_names = { [PB_UP] = u"방향 위", [PB_DOWN] = u"방향 아래", [PB_LEFT] = u"방향 왼쪽", [PB_RIGHT] = u"방향 오른쪽",
                   [PB_SQUARE] = u"A 버튼", [PB_CROSS] = u"B 버튼 (SMS 1)", [PB_CIRCLE] = u"C 버튼 (SMS 2)",
                   [PB_L1] = u"X 버튼", [PB_TRIANGLE] = u"Y 버튼", [PB_R1] = u"Z 버튼",
                   [PB_START] = u"START", [PB_SELECT] = u"MODE" },
    .bar = { PB_L1, PB_SELECT, PB_START, PB_R1, -1 },
    .bar_names = { u"X", u"MOD", u"STA", u"Z" },
    .pad_names = { u"Y", u"C", u"B", u"A" },
    .apply = apply,
};
