# Vendored from waveshare/pcf85063a v1.1.1, patched for ESP-IDF v6:
# added "esp_driver_i2c" to REQUIRES (v6 split driver/i2c_master.h out of "driver").
idf_component_register(SRCS "pcf85063a.c"  INCLUDE_DIRS "include" REQUIRES "driver" "esp_driver_i2c")