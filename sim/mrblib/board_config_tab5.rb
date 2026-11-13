# BoardConfig for the simulator's Tab5 (cf. components/midori-runtime/mrblib/board_config.rb.in)
module BoardConfig
  BOARD_NAME = "M5Stack Tab5 (simulator)"
  SD_MODE = "sim"
  SD_CLK_PIN = -1
  SD_CMD_PIN = -1
  SD_D0_PIN  = -1
  SD_SLOT = -1
  SD_FREQ_KHZ = -1
  SD_SCK_PIN  = -1
  SD_MISO_PIN = -1
  SD_MOSI_PIN = -1
  SD_CS_PIN   = -1
  SD_SPI_UNIT = :ESP32_SPI2_HOST
  SAM2695_TX_PIN = 53
  SAM2695_RX_PIN = 54
  HAS_SAM2695 = true
  HAS_USB_MIDI_HOST = true
  HAS_USB_MIDI_DEVICE = true
  HAS_AMY = true           # rendered by the page (sim/port/amy_platform_sim.c, sim/web/amy_audio.js)
  HAS_BLE_MIDI = false
  HAS_PRIMARY_I2C = false
  PRIMARY_I2C_SDA_PIN = -1
  PRIMARY_I2C_SCL_PIN = -1
  PRIMARY_I2C_UNIT = ""
end
