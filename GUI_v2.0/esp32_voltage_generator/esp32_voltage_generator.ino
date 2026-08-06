/*
Flash command:
~/.local/bin/esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash -z \
  0x0 esp32_voltage_generator.ino.bootloader.bin \
  0x8000 esp32_voltage_generator.ino.partitions.bin \
  0x10000 esp32_voltage_generator.ino.bin
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// SPI pin definitions
#define NANO_CS_PIN 5 // chip select
#define RST_PIN 11 // reset

// ── I2C bus pin definitions ───────────────────────────────────────────────────
#define BANK0_SDA 3
#define BANK0_SCL 4

#define BANK1_SDA 5
#define BANK1_SCL 6

#define BANK2_SDA 9  // Bit-bang I2C
#define BANK2_SCL 10  // Bit-bang I2C

// ── I2C bus objects (hardware only for banks 0 & 1) ──────────────────────────
TwoWire* const i2cHW[2] = { &Wire, &Wire1 };

// ── DAC70501 I2C addresses ────────────────────────────────────────────────────
const uint8_t DAC_ADDR[4] = { 0x48, 0x49, 0x4A, 0x4B };

// ── DAC70501 registers ────────────────────────────────────────────────────────
const uint8_t REG_SYNC = 0x01;    //0x01;
const uint8_t REG_CONFIG = 0x03;  //0x02;
const uint8_t REG_GAIN = 0x04;
const uint8_t REG_TRIGGER = 0x05;
const uint8_t REG_DAC = 0x08;

// ── DAC count ─────────────────────────────────────────────────────────────────
const uint8_t NUM_BANKS = 3;
const uint8_t DACS_PER_BANK = 4;
const uint8_t TOTAL_DACS = NUM_BANKS * DACS_PER_BANK;  // 12

// =============================================================================
//  Bit-bang I2C for Bank 2  (no library, ESP32-safe)
// =============================================================================

#define SW_HALF_PERIOD_US 5  // 5 µs half-period → ~100 kHz

#define SDA_HIGH() pinMode(BANK2_SDA, INPUT)
#define SDA_LOW() \
  do { \
    digitalWrite(BANK2_SDA, LOW); \
    pinMode(BANK2_SDA, OUTPUT); \
  } while (0)
#define SCL_HIGH() pinMode(BANK2_SCL, INPUT)
#define SCL_LOW() \
  do { \
    digitalWrite(BANK2_SCL, LOW); \
    pinMode(BANK2_SCL, OUTPUT); \
  } while (0)
#define SDA_READ() digitalRead(BANK2_SDA)

void sw_init() { // serial writing initialization
  digitalWrite(BANK2_SDA, LOW);
  digitalWrite(BANK2_SCL, LOW);
  SDA_HIGH();
  SCL_HIGH();
}

void sw_start() {
  SDA_HIGH();
  delayMicroseconds(SW_HALF_PERIOD_US);
  SCL_HIGH();
  delayMicroseconds(SW_HALF_PERIOD_US);
  SDA_LOW();
  delayMicroseconds(SW_HALF_PERIOD_US);
  SCL_LOW();
  delayMicroseconds(SW_HALF_PERIOD_US);
}

void sw_stop() {
  SDA_LOW();
  delayMicroseconds(SW_HALF_PERIOD_US);
  SCL_HIGH();
  delayMicroseconds(SW_HALF_PERIOD_US);
  SDA_HIGH();
  delayMicroseconds(SW_HALF_PERIOD_US);
}

bool sw_writeByte(uint8_t data) {
  for (int i = 7; i >= 0; i--) {
    if (data & (1 << i)) {
      SDA_HIGH();
    } else {
      SDA_LOW();
    }
    delayMicroseconds(SW_HALF_PERIOD_US);
    SCL_HIGH();
    delayMicroseconds(SW_HALF_PERIOD_US);
    SCL_LOW();
    delayMicroseconds(SW_HALF_PERIOD_US);
  }
  SDA_HIGH();
  delayMicroseconds(SW_HALF_PERIOD_US);
  SCL_HIGH();
  delayMicroseconds(SW_HALF_PERIOD_US);
  bool ack = (SDA_READ() == LOW);
  SCL_LOW();
  delayMicroseconds(SW_HALF_PERIOD_US);
  return ack;
}

bool sw_writeReg(uint8_t addr, uint8_t reg, uint16_t value) {
  sw_start();
  bool ok = true; // ok will be false if one of the following lines fails
  ok &= sw_writeByte((addr << 1) | 0x00); // address is 7bits, but we received 8 bits, so must shift left 1 bit
  // LSB is filled with 0. it's the R/W bit. 0=write, 1=read
  // | 0x00 puts a write bit, 0x01 puts a read bit
  // we don't actually need 0x00, it's redudant here
  ok &= sw_writeByte(reg);
  ok &= sw_writeByte((uint8_t)(value >> 8)); // writes the high half (most significant 8 bits)
  ok &= sw_writeByte((uint8_t)(value & 0xFF)); // writes the low half (least sig 8 bits)
  sw_stop();
  return ok;
}

bool sw_probe(uint8_t addr) {
  sw_start();
  bool ack = sw_writeByte((addr << 1) | 0x00);
  sw_stop();
  return ack;
}

// =============================================================================
//  Unified low-level I2C write (all 3 banks)
// =============================================================================

bool dacWriteReg(uint8_t bank, uint8_t chip, uint8_t reg, uint16_t value) {
  if (bank == 2) {
    return sw_writeReg(DAC_ADDR[chip], reg, value);
  } else {
    TwoWire* bus = i2cHW[bank];
    bus->beginTransmission(DAC_ADDR[chip]);
    bus->write(reg);
    bus->write((uint8_t)(value >> 8));
    bus->write((uint8_t)(value & 0xFF));
    return (bus->endTransmission() == 0);
  }
}

// =============================================================================
//  Chip initialisation & Voltage Helpers
// =============================================================================

bool dacInit(uint8_t bank, uint8_t chip) {
  bool ok = true;
  ok &= dacWriteReg(bank, chip, REG_TRIGGER, 0x000A);
  delay(1);
  ok &= dacWriteReg(bank, chip, REG_SYNC, 0x0000);
  ok &= dacWriteReg(bank, chip, REG_CONFIG, 0x0000);
  ok &= dacWriteReg(bank, chip, REG_GAIN, 0x0001);
  ok &= dacWriteReg(bank, chip, REG_DAC, 0x0000);
  return ok;
}

float codeToVoltage(uint16_t code) {
  return ((float)code / 65536.0f) * 5.0f;
}

// =============================================================================
//  Setup
// =============================================================================

void setup() {
  Serial.begin(115200);

  // Initialize the custom CS pin as an output
  pinMode(NANO_CS_PIN, OUTPUT);

  // SPI CS lines are active LOW; keep it HIGH to start deselected
  digitalWrite(NANO_CS_PIN, HIGH);

  // Initialize the main SPI bus
  SPI.begin();

  // Power on the TFT rail and backlight
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  // Init display: 135x240, landscape
  tft.init(135, 240);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("Initializing...");

  // Hardware I2C buses
  Wire.begin(BANK0_SDA, BANK0_SCL);
  Wire1.begin(BANK1_SDA, BANK1_SCL);
  Wire.setClock(400000UL);
  Wire1.setClock(400000UL);

  // Bit-bang I2C bus (Bank 2)
  sw_init();

  // Scan and init all DACs silently
  for (uint8_t bank = 0; bank < NUM_BANKS; bank++) {
    for (uint8_t chip = 0; chip < DACS_PER_BANK; chip++) {
      if (bank == 2) {
        if (sw_probe(DAC_ADDR[chip])) dacInit(bank, chip);
      } else {
        i2cHW[bank]->beginTransmission(DAC_ADDR[chip]);
        if (i2cHW[bank]->endTransmission() == 0) {
          dacInit(bank, chip);
        }
      }
    }
  }

  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.println("Ready to receive.");
}

// =============================================================================
//  SPI communication
// =============================================================================
void sendDataToNano(byte data) {
  // Send data at 1MHz, MSBFIRST, SPI_MODE0
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));

  // Select the Nano
  digitalWrite(NANO_CS_PIN, LOW);

  // Print the Nano's acknowledgement 0xF1
  uint8_t response_from_nano = SPI.transfer(data);
  //Serial.print("Response from Nano: 0x");
  //Serial.println(response_from_nano, HEX);
  
  // These commands will be read by the GUI
  Serial.print("Received: "); // receiver
  Serial.println(response_from_nano, HEX); // newline after each byte

  // Deselect the Nano
  digitalWrite(NANO_CS_PIN, HIGH);

  SPI.endTransaction();
}

// =============================================================================
//  Main loop
// =============================================================================

const uint8_t MSG_LEN = 5; // expecting 5 bytes
uint8_t msg_buf[MSG_LEN];

void loop() {

  if (Serial.available() >= MSG_LEN) { // waiting for 5 bytes to arrive
    Serial.readBytes(msg_buf, MSG_LEN);

    uint8_t bank = msg_buf[0];
    uint8_t chip = msg_buf[1];
    uint8_t reg = msg_buf[2];
    uint8_t high = msg_buf[3];
    uint8_t low = msg_buf[4];
    uint16_t code = ((uint16_t)high << 8) | low;
    dacWriteReg(bank, chip, REG_DAC, code);

    // Update TFT
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(3);

    // Row 1: DAC Number
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(0, 20);
    tft.print("DAC: ");
    tft.setTextColor(ST77XX_WHITE);
    tft.print(bank * chip);

    if ((reg == 0x03) && (code == 0x0001)) {
      tft.setTextColor(ST77XX_RED);
      tft.println(" OFF");
    } else if ((reg == 0x02) && (code == 0x0000)) {
      tft.setTextColor(ST77XX_GREEN);
      tft.println(" ON");
    }

    // Row 2: Voltage
    float voltage = codeToVoltage(code);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(0, 70);
    tft.println("VOLTAGE:");
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(0, 100);
    tft.print(voltage, 4);
    tft.print(" V");
    
  }

}
