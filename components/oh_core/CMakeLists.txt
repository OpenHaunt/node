idf_component_register(
    SRCS "src/oh_port.c" "src/oh_module.c" "src/oh_descriptor.c" "src/oh_json.c"
         "src/oh_vmod.c" "src/oh_debounce.c" "src/oh_e131.c"
         "src/oh_curve.c" "src/oh_effect.c" "src/oh_clock_sync.c"
         "src/oh_wav.c" "src/oh_rtp.c" "src/oh_mixer.c" "src/oh_audio_proto.c"
         "src/oh_pixel.c" "src/oh_pixfx.c" "src/oh_sacn_merge.c" "src/oh_brokers.c"
    INCLUDE_DIRS "include"
    REQUIRES json)
