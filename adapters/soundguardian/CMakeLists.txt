# ESP-IDF component: edge-audio core + SoundGuardian adapter (K-01, K-02).
# The core sources are compiled here with port_esp.c instead of port_posix.c.
idf_component_register(
  SRCS
    "profile.c"
    "es8311_i2s.c"
    "port_esp.c"
    "../../src/flags.c"
    "../../src/config.c"
    "../../src/stream.c"
    "../../src/queue.c"
    "../../src/events.c"
    "../../src/history.c"
    "../../src/version.c"
  INCLUDE_DIRS "." "../../include"
  PRIV_INCLUDE_DIRS "../../src"
  REQUIRES driver esp_driver_i2c esp_common log
)
