#include <SPI.h>
#include <util/atomic.h>

const uint8_t responseBytes[3] = {0x11, 0x05, 0x01};
volatile uint8_t responseIndex = 1;

const uint8_t RX_BUFFER_SIZE = 8;
volatile uint8_t rxBuffer[RX_BUFFER_SIZE];
volatile uint8_t rxHead = 0;
volatile uint8_t rxTail = 0;
volatile bool rxOverflow = false;

// Classic Nano / ATmega328P hardware SPI pins.
const uint8_t NANO_MISO_PIN = 12;
const uint8_t NANO_MOSI_PIN = 11;
const uint8_t NANO_SCK_PIN = 13;
const uint8_t NANO_SS_PIN = 10;

void setup() {
  Serial.begin(115200);

  // Set MISO (D12) as OUTPUT so the Nano can send data back if needed
  pinMode(NANO_MISO_PIN, OUTPUT);
  
  // Set MOSI (D11), SCK (D13), and SS (D10) as INPUTs (Automatic via hardware, but good practice)
  pinMode(NANO_MOSI_PIN, INPUT);
  pinMode(NANO_SCK_PIN, INPUT);
  pinMode(NANO_SS_PIN, INPUT_PULLUP);

  // Explicit SPI peripheral settings: Mode 0, MSB first, interrupts enabled.
  // MSTR, CPOL, CPHA, and DORD are all left clear.
  SPCR = _BV(SPE) | _BV(SPIE);
  SPSR = 0;

  // MISO must be loaded before the controller clocks the first byte.
  SPDR = responseBytes[0];
  responseIndex = 1;

  // Nano D10/SS is PB2/PCINT2. Reset the fixed response sequence on each
  // falling edge of CS; the Feather provides a 10 us CS setup interval.
  PCIFR = _BV(PCIF0);
  PCMSK0 |= _BV(PCINT2);
  PCICR |= _BV(PCIE0);

  Serial.println("READY: SPI peripheral MODE0 MSB FIRST");
}

// CS falling begins a new 1-3-byte command and preloads its first MISO byte.
ISR(PCINT0_vect) {
  if ((PINB & _BV(PB2)) == 0) {
    SPDR = responseBytes[0];
    responseIndex = 1;
  }
}

// Runs after one complete byte. A response derived from the received byte can
// only be shifted out during the next controller transfer (one-byte latency).
ISR(SPI_STC_vect) {
  const uint8_t received = SPDR;  // Save MOSI before staging the next MISO byte.

  const uint8_t nextHead = (uint8_t)((rxHead + 1) % RX_BUFFER_SIZE);
  if (nextHead != rxTail) {
    rxBuffer[rxHead] = received;
    rxHead = nextHead;
  } else {
    rxOverflow = true;
  }

  SPDR = responseBytes[responseIndex];
  responseIndex = (uint8_t)((responseIndex + 1) % 3);
}

bool popReceivedByte(uint8_t& value) {
  bool available = false;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (rxTail != rxHead) {
      value = rxBuffer[rxTail];
      rxTail = (uint8_t)((rxTail + 1) % RX_BUFFER_SIZE);
      available = true;
    }
  }
  return available;
}

bool takeOverflowFlag() {
  bool overflowed;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    overflowed = rxOverflow;
    rxOverflow = false;
  }
  return overflowed;
}

void printHexByte(uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

void loop() {
  uint8_t received;
  while (popReceivedByte(received)) {
    Serial.print("RX: ");
    printHexByte(received);
    Serial.println();
  }

  if (takeOverflowFlag()) Serial.println("ERR: SPI RX buffer overflow");
}

