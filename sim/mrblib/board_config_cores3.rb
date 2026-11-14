# BoardConfig for the simulator's CoreS3 in host mode (cf. components/midori-runtime/mrblib/board_config.rb.in)
module BoardConfig
  BOARD_NAME = "M5Stack CoreS3 (simulator)"
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
  SAM2695_TX_PIN = 17
  SAM2695_RX_PIN = 18
  HAS_SAM2695 = true
  HAS_USB_MIDI_HOST = true      # host mode: USB-C is the USB-MIDI host
  HAS_USB_MIDI_DEVICE = false   # (and so no USB-MIDI device, as on the device)
  HAS_AMY = false               # AMY plays on the P4 boards only
  HAS_BLE_MIDI = false
  HAS_PRIMARY_I2C = false
  PRIMARY_I2C_SDA_PIN = -1
  PRIMARY_I2C_SCL_PIN = -1
  PRIMARY_I2C_UNIT = ""
end
