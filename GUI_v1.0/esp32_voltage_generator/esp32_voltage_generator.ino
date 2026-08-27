/*
adjust_voltage.ino
Combined DAC I2C Controller & TFT Display for Adafruit Feather ESP32-S3 Reverse TFT

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
#include <ctype.h>

#if !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_REVTFT)
#error "Select Tools > Board > Adafruit Feather ESP32-S3 Reverse TFT"
#endif

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// External SPI header on the Adafruit ESP32-S3 Reverse TFT Feather.
// No chip-select is assigned until the real peripheral CS net is confirmed.
#define EXT_SPI_SCK_PIN 36
#define EXT_SPI_MISO_PIN 37
#define EXT_SPI_MOSI_PIN 35
#define EXT_SPI_CLOCK_HZ 1000000UL

// Preserve the pre-repair state of D13 exactly. It remains HIGH continuously
// and is deliberately not used or toggled as SPI chip-select in this test.
#define PRESERVED_D13_PIN 13

const char FIRMWARE_ID[] = "GUI_v1.0 PINSAFE-D13-HIGH 2026-08-20";

// ── I2C bus pin definitions ───────────────────────────────────────────────────
#define BANK0_SDA 3  //21
#define BANK0_SCL 4  //22

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

// ── Voltage constants ─────────────────────────────────────────────────────────
const float VREF = 2.5f;
const float VFULL_SCALE = 5.0f;

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

void sw_init() {
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
  bool ok = true;
  ok &= sw_writeByte((addr << 1) | 0x00);
  ok &= sw_writeByte(reg);
  ok &= sw_writeByte((uint8_t)(value >> 8));
  ok &= sw_writeByte((uint8_t)(value & 0xFF));
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

uint16_t voltageToCode(float v) {
  if (v <= 0.0f) return 0x0000;
  if (v >= VFULL_SCALE) return 0xFFFF;
  uint32_t code = (uint32_t)(v / VFULL_SCALE * 65536.0f + 0.5f);
  return (uint16_t)(code > 0xFFFF ? 0xFFFF : code);
}

// =============================================================================
//  Setup
// =============================================================================

void setup() {
  Serial.begin(115200);

  // Restore the original non-SPI pin state before initializing SPI.
  pinMode(PRESERVED_D13_PIN, OUTPUT);
  digitalWrite(PRESERVED_D13_PIN, HIGH);

  // Bind only the dedicated SCK/MISO/MOSI header pins. Passing -1 for SS keeps
  // the SPI library from claiming any DAC, rail-control, or unconfirmed CS pin.
  SPI.begin(EXT_SPI_SCK_PIN, EXT_SPI_MISO_PIN, EXT_SPI_MOSI_PIN, -1);

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

  // Give the external bias board time to power up before probing its DACs.
  delay(250);

  Serial.print("FW: ");
  Serial.println(FIRMWARE_ID);
  Serial.println("PIN: D13=HIGH SPI_CS=DISABLED SPI=SCK36/MISO37/MOSI35");

  // Scan and initialize all DACs, reporting whether each device acknowledged.
  for (uint8_t bank = 0; bank < NUM_BANKS; bank++) {
    for (uint8_t chip = 0; chip < DACS_PER_BANK; chip++) {
      bool found = false;
      if (bank == 2) {
        found = sw_probe(DAC_ADDR[chip]);
      } else {
        i2cHW[bank]->beginTransmission(DAC_ADDR[chip]);
        found = (i2cHW[bank]->endTransmission() == 0);
      }

      bool initialized = found && dacInit(bank, chip);
      Serial.print("I2C: bank=");
      Serial.print(bank);
      Serial.print(" addr=0x");
      printHexByte(DAC_ADDR[chip]);
      Serial.print(" status=");
      Serial.println(initialized ? "OK" : (found ? "INIT_ERR" : "NO_ACK"));
    }
  }

  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.println("Ready to receive.");
  tft.setTextSize(1);
  tft.setCursor(0, 28);
  tft.println("PINSAFE-D13-HIGH");
}

// =============================================================================
//  SPI communication
// =============================================================================
void printHexByte(uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

bool parseHexByteToken(const String& token, uint8_t& value) {
  if (token.length() < 1 || token.length() > 2) return false;
  for (size_t i = 0; i < token.length(); i++) {
    if (!isxdigit((unsigned char)token[i])) return false;
  }

  char* end = nullptr;
  unsigned long parsed = strtoul(token.c_str(), &end, 16);
  if (!end || *end != '\0' || parsed > 0xFFUL) return false;
  value = (uint8_t)parsed;
  return true;
}

void sendSpiCommand(const uint8_t* tx, uint8_t count) {
  if (!tx || count < 1 || count > 3) return;

  uint8_t rx[3] = {0};
  SPI.beginTransaction(SPISettings(EXT_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
  for (uint8_t i = 0; i < count; i++) {
    rx[i] = SPI.transfer(tx[i]);
  }
  SPI.endTransaction();

  Serial.print("TX:");
  for (uint8_t i = 0; i < count; i++) {
    Serial.print(' ');
    printHexByte(tx[i]);
  }
  Serial.println();

  for (uint8_t i = 0; i < count; i++) {
    Serial.print("RX: ");
    printHexByte(rx[i]);
    Serial.println();
  }
}

// =============================================================================
//  Main loop
// =============================================================================

void loop() {

  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');  // Read until newline
    msg.trim();
    if (msg.length() == 0) return;  // Empty msg

    Serial.print("CMD: ");
    Serial.println(msg);

    // Get protocol bit to determine if we send through I2C or SPI
    int firstSpaceIdx = msg.indexOf(' ');
    if (firstSpaceIdx < 0) return;  // Malformed msg
    String protocolStr = msg.substring(0, firstSpaceIdx);
    if (protocolStr != "0" && protocolStr != "1") {
      Serial.println("ERR: protocol must be 0 or 1");
      return;
    }
    bool protocol = (protocolStr == "1");

    // I2C variables
    String offStr, dacStr, voltStr;

    // If protocol bit is 1, choose I2C
    if (protocol) {
      int secondSpaceIdx = msg.indexOf(' ', firstSpaceIdx + 1);  // space btwn off-dac
      if (secondSpaceIdx < 0) return;                            // Malformed msg

      int thirdSpaceIdx = msg.indexOf(' ', secondSpaceIdx + 1);  // space btwn dac-voltage
      if (thirdSpaceIdx < 0) return;                             // Malformed msg

      offStr = msg.substring(firstSpaceIdx + 1, secondSpaceIdx);
      dacStr = msg.substring(secondSpaceIdx + 1, thirdSpaceIdx);
      voltStr = msg.substring(thirdSpaceIdx + 1);
      Serial.println(voltStr);

      // I2C continues here
      int offValue = offStr.toInt();
      int dacIndex = dacStr.toInt();
      float voltage = voltStr.toFloat();

      // Bounds check
      if (dacIndex < 0 || dacIndex >= TOTAL_DACS) return;
      if (voltage < 0.0f) voltage = 0.0f;
      if (voltage > VFULL_SCALE) voltage = VFULL_SCALE;

      // Write voltage and power state to the DAC. Check both acknowledgements;
      // the TFT must not claim success when the external board is unreachable.
      uint8_t bank = (uint8_t)dacIndex / DACS_PER_BANK;
      uint8_t chip = (uint8_t)dacIndex % DACS_PER_BANK;
      uint16_t code = voltageToCode(voltage);
      bool voltageOk = dacWriteReg(bank, chip, REG_DAC, code);
      bool configOk = dacWriteReg(bank, chip, REG_CONFIG, offValue ? 0x0001 : 0x0000);
      bool dacOk = voltageOk && configOk;

      Serial.print("DAC: bank=");
      Serial.print(bank);
      Serial.print(" addr=0x");
      printHexByte(DAC_ADDR[chip]);
      Serial.print(" code=0x");
      if (code < 0x1000) Serial.print('0');
      if (code < 0x0100) Serial.print('0');
      if (code < 0x0010) Serial.print('0');
      Serial.print(code, HEX);
      Serial.print(" power=");
      Serial.print(offValue ? "OFF" : "ON");
      Serial.print(" status=");
      Serial.println(dacOk ? "OK" : "NO_ACK");

      // Update TFT
      //--------------------------------------------------
      tft.fillScreen(ST77XX_BLACK);
      tft.setTextSize(3);

      // Row 1: DAC Number
      tft.setTextColor(ST77XX_CYAN);
      tft.setCursor(0, 20);
      tft.print("DAC: ");
      tft.setTextColor(ST77XX_WHITE);
      tft.print(dacIndex);
      
      if (offValue) {
        //---------------------------------------
        tft.setTextColor(ST77XX_RED);
        tft.println(" OFF");
        //--------------------------------------
      } else {
        //----------------------------------------------------
        tft.setTextColor(ST77XX_GREEN);
        //----------------------------------------------------
        tft.println(" ON");
      }

      // Row 2: Voltage
      
      tft.setTextColor(ST77XX_CYAN);
      tft.setCursor(0, 70);
      tft.println("VOLTAGE:");
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(0, 100);
      if (dacOk) {
        tft.print(voltage, 4);  // Display up to 4 decimal places
        tft.print(" V");
      } else {
        tft.setTextColor(ST77XX_RED);
        tft.print("I2C ERROR");
      }
    }

    // If protocol bit is 0,choose SPI
    else {
      String rest = msg.substring(firstSpaceIdx + 1);
      rest.trim();

      uint8_t tx[3] = {0};
      uint8_t numBytes = 0;
      int position = 0;
      while (position < (int)rest.length()) {
        while (position < (int)rest.length() && isspace((unsigned char)rest[position])) position++;
        if (position >= (int)rest.length()) break;

        int tokenStart = position;
        while (position < (int)rest.length() && !isspace((unsigned char)rest[position])) position++;
        String token = rest.substring(tokenStart, position);

        if (numBytes >= 3) {
          Serial.println("ERR: SPI command accepts one to three bytes");
          return;
        }
        if (!parseHexByteToken(token, tx[numBytes])) {
          Serial.print("ERR: invalid SPI hex byte: ");
          Serial.println(token);
          return;
        }
        numBytes++;
      }

      if (numBytes == 0) {
        Serial.println("ERR: SPI command contains no data bytes");
        return;
      }
      sendSpiCommand(tx, numBytes);
    }
  }
}
