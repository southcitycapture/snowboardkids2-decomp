ASSERT(
    main_VRAM_END <= rand_VRAM,
    "Resident main image exceeds its RAM budget and overlaps the rand overlay at 0x800AFF30"
);

ASSERT(
    rand_VRAM_END <= race_VRAM,
    "rand overlay exceeds its RAM budget and overlaps overlays at 0x800B00C0"
);

/* The race overlay remains resident while a course overlay is loaded. */
ASSERT(
    race_VRAM_END <= levels_lindas_castle_VRAM,
    "race overlay exceeds its RAM budget and overlaps course overlays at 0x800BB2B0"
);

/* Menu overlays loaded at 0x800B00C0 use the same lower overlay partition. */
ASSERT(
    CHARACTER_SELECT_SPRITES_OVERLAY_VRAM_END <= levels_lindas_castle_VRAM,
    "character-select sprites overlay exceeds the 0x800BB2B0 RAM boundary"
);
ASSERT(
    PLAYER_SELECT_SPRITES_OVERLAY_VRAM_END <= levels_lindas_castle_VRAM,
    "player-select sprites overlay exceeds the 0x800BB2B0 RAM boundary"
);
ASSERT(
    credits_VRAM_END <= levels_lindas_castle_VRAM,
    "credits overlay exceeds the 0x800BB2B0 RAM boundary"
);

/* The editor widgets immediately follow, and may coexist with, cutscene code. */
ASSERT(
    cutscene_VRAM_END <= cutscene_editor_widgets_VRAM,
    "cutscene overlay overlaps editor widgets at 0x800BAF10"
);

/* Course overlays and the optional editor widgets must not reach static buffers. */
ASSERT(levels_lindas_castle_VRAM_END <= buffers_bss_VRAM, "Linda's Castle overlay overlaps buffers at 0x8016A000");
ASSERT(levels_crazy_jungle_boss_VRAM_END <= buffers_bss_VRAM, "Crazy Jungle boss overlay overlaps buffers at 0x8016A000");
ASSERT(levels_snowboard_street_shoot_cross_VRAM_END <= buffers_bss_VRAM, "Shoot Cross overlay overlaps buffers at 0x8016A000");
ASSERT(levels_starlight_highway_VRAM_END <= buffers_bss_VRAM, "Starlight Highway overlay overlaps buffers at 0x8016A000");
ASSERT(levels_haunted_house_VRAM_END <= buffers_bss_VRAM, "Haunted House overlay overlaps buffers at 0x8016A000");
ASSERT(levels_ice_land_boss_VRAM_END <= buffers_bss_VRAM, "Ice Land boss overlay overlaps buffers at 0x8016A000");
ASSERT(levels_ice_land_VRAM_END <= buffers_bss_VRAM, "Ice Land overlay overlaps buffers at 0x8016A000");
ASSERT(levels_snowboard_street_speed_cross_VRAM_END <= buffers_bss_VRAM, "Speed Cross overlay overlaps buffers at 0x8016A000");
ASSERT(levels_jingle_town_boss_VRAM_END <= buffers_bss_VRAM, "Jingle Town boss overlay overlaps buffers at 0x8016A000");
ASSERT(levels_jingle_town_VRAM_END <= buffers_bss_VRAM, "Jingle Town overlay overlaps buffers at 0x8016A000");
ASSERT(levels_sunny_mountain_VRAM_END <= buffers_bss_VRAM, "Sunny Mountain overlay overlaps buffers at 0x8016A000");
ASSERT(levels_wendys_house_VRAM_END <= buffers_bss_VRAM, "Wendy's House overlay overlaps buffers at 0x8016A000");
ASSERT(levels_training_VRAM_END <= buffers_bss_VRAM, "training overlay overlaps buffers at 0x8016A000");
ASSERT(levels_crazy_jungle_VRAM_END <= buffers_bss_VRAM, "Crazy Jungle overlay overlaps buffers at 0x8016A000");
ASSERT(cutscene_editor_widgets_VRAM_END <= buffers_bss_VRAM, "cutscene editor widgets overlap buffers at 0x8016A000");
