// Driver definition for HopeRF RFM69W/RFM69HW, Semtech SX1231/1231H
// Creative Commons Attrib Share-Alike License
// If you want to use/extend this library please abide with the license terms: http://creativecommons.org/licenses/by-sa/3.0/
// 2013-06-14 (C) felix@lowpowerlab.com

#include <RFM69.h>
#include <RFM69registers.h>
#include <SPI.h>

RFM69* RFM69::selfPointer;

bool RFM69::initialize(byte freqBand, byte nodeID, byte networkID)
{
  const byte CONFIG[][2] =
  {
    /* 0x01 */ { REG_OPMODE, RF_OPMODE_SEQUENCER_ON | RF_OPMODE_LISTEN_OFF | RF_OPMODE_STANDBY},
    /* 0x02 */ { REG_DATAMODUL, RF_DATAMODUL_DATAMODE_PACKET | RF_DATAMODUL_MODULATIONTYPE_FSK | RF_DATAMODUL_MODULATIONSHAPING_00}, //gaussian, bt=1.0
    /* 0x03 */ { REG_BITRATEMSB, RF_BITRATEMSB_50000}, //default is 4.8 KBPS
    /* 0x04 */ { REG_BITRATELSB, RF_BITRATELSB_50000},
    /* 0x05 */ { REG_FDEVMSB, RF_FDEVMSB_50000}, //default is 5khz
    /* 0x06 */ { REG_FDEVLSB, RF_FDEVLSB_50000},

    /* 0x07 */ { REG_FRFMSB, (freqBand==RF69_315MHZ ? RF_FRFMSB_315 : (freqBand==RF69_433MHZ ? RF_FRFMSB_433 : (freqBand==RF69_868MHZ ? RF_FRFMSB_868 : RF_FRFMSB_915)))},
    /* 0x08 */ { REG_FRFMID, (freqBand==RF69_315MHZ ? RF_FRFMID_315 : (freqBand==RF69_433MHZ ? RF_FRFMID_433 : (freqBand==RF69_868MHZ ? RF_FRFMID_868 : RF_FRFMID_915)))},
    /* 0x09 */ { REG_FRFLSB, (freqBand==RF69_315MHZ ? RF_FRFLSB_315 : (freqBand==RF69_433MHZ ? RF_FRFLSB_433 : (freqBand==RF69_868MHZ ? RF_FRFLSB_868 : RF_FRFLSB_915)))},
    
    // looks like PA1 and PA2 are not implemented on RFM69W, hence the max output power is 13dBm
    // +17dBm and +20dBm are possible on RFM69HW
    // +13dBm formula: Pout=-18+OutputPower (with PA0 or PA1**)
    // +17dBm formula: Pout=-14+OutputPower (with PA1 and PA2)**
    // +20dBm formula: Pout=-11+OutputPower (with PA1 and PA2)** and high power PA settings (section 3.3.7 in datasheet)
    ///* 0x11 */ { REG_PALEVEL, RF_PALEVEL_PA0_ON | RF_PALEVEL_PA1_OFF | RF_PALEVEL_PA2_OFF | RF_PALEVEL_OUTPUTPOWER_11111},
    /* 0x19 */ { REG_RXBW, RF_RXBW_DCCFREQ_010 | RF_RXBW_MANT_24 | RF_RXBW_EXP_2},
    /* 0x25 */ { REG_DIOMAPPING1, RF_DIOMAPPING1_DIO0_01}, //DIO0 is the only IRQ we're using
    /* 0x29 */ { REG_RSSITHRESH, 220}, // must be set to dBm = (-Sensitivity / 2) - default is 0xE4=228 so -114dBm
    ///* 0x2d */ { REG_PREAMBLELSB, RF_PREAMBLESIZE_LSB_VALUE } // default 3 preamble bytes 0xAAAAAA
    /* 0x2e */ { REG_SYNCCONFIG, RF_SYNC_ON | RF_SYNC_FIFOFILL_AUTO | RF_SYNC_SIZE_2 | RF_SYNC_TOL_0},
    /* 0x2f */ { REG_SYNCVALUE1, 0x2D},      //attempt to make this compatible with sync1 byte of RFM12B lib
    /* 0x30 */ { REG_SYNCVALUE2, networkID}, //NETWORK ID
    /* 0x37 */ { REG_PACKETCONFIG1, RF_PACKET1_FORMAT_VARIABLE | RF_PACKET1_DCFREE_OFF | RF_PACKET1_CRC_ON | RF_PACKET1_CRCAUTOCLEAR_ON | RF_PACKET1_ADRSFILTERING_OFF },
    /* 0x38 */ { REG_PAYLOADLENGTH, 66 }, //in variable length mode: the max frame size, not used in TX
    //* 0x39 */ { REG_NODEADRS, nodeID}, //turned off because we're not using address filtering
    /* 0x3d */ { REG_PACKETCONFIG2, RF_PACKET2_RXRESTARTDELAY_2BITS | RF_PACKET2_AUTORXRESTART_ON | RF_PACKET2_AES_OFF }, //RXRESTARTDELAY must match transmitter PA ramp-down time (bitrate dependent)
    /* 0x6F */ { REG_TESTDAGC, RF_DAGC_CONTINUOUS }, // run DAGC continuously in RX mode
    {255, 0}
  };
  
  pinMode(_slaveSelectPin, OUTPUT);
  unselect();
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV2); //max speed, except on Due which can run at system clock speed
  SPI.begin();
  
  do writeReg(REG_SYNCVALUE1, 0xaa); while (readReg(REG_SYNCVALUE1) != 0xaa);
	do writeReg(REG_SYNCVALUE1, 0x55); while (readReg(REG_SYNCVALUE1) != 0x55);
  
  for (byte i = 0; CONFIG[i][0] != 255; i++)
    writeReg(CONFIG[i][0], CONFIG[i][1]);

  setHighPower(_isRFM69HW); //called regardless if it's a RFM69W or RFM69HW
  setMode(RF69_MODE_STANDBY);
	while ((readReg(REG_IRQFLAGS1) & RF_IRQFLAGS1_MODEREADY) == 0x00); // Wait for ModeReady
  attachInterrupt(0, RFM69::isr0, RISING);
  
  selfPointer = this;
  _address = nodeID;
}

void RFM69::setFrequency(uint32_t FRF)
{
  writeReg(REG_FRFMSB, FRF >> 16);
  writeReg(REG_FRFMID, FRF >> 8);
  writeReg(REG_FRFLSB, FRF);
}

void RFM69::setMode(byte newMode)
{
	if (newMode == _mode) return; //TODO: can remove this?

	switch (newMode) {
		case RF69_MODE_TX:
			writeReg(REG_OPMODE, (readReg(REG_OPMODE) & 0xE3) | RF_OPMODE_TRANSMITTER);
      if (_isRFM69HW) setHighPowerRegs(true);
			break;
		case RF69_MODE_RX:
			writeReg(REG_OPMODE, (readReg(REG_OPMODE) & 0xE3) | RF_OPMODE_RECEIVER);
      if (_isRFM69HW) setHighPowerRegs(false);
			break;
		case RF69_MODE_SYNTH:
			writeReg(REG_OPMODE, (readReg(REG_OPMODE) & 0xE3) | RF_OPMODE_SYNTHESIZER);
			break;
		case RF69_MODE_STANDBY:
			writeReg(REG_OPMODE, (readReg(REG_OPMODE) & 0xE3) | RF_OPMODE_STANDBY);
			break;
		case RF69_MODE_SLEEP:
			writeReg(REG_OPMODE, (readReg(REG_OPMODE) & 0xE3) | RF_OPMODE_SLEEP);
			break;
		default: return;
	}

	// we are using packet mode, so this check is not really needed
  // but waiting for mode ready is necessary when going from to sleep because the FIFO may not be immediately available from previous mode
	while (_mode == RF69_MODE_SLEEP && (readReg(REG_IRQFLAGS1) & RF_IRQFLAGS1_MODEREADY) == 0x00); // Wait for ModeReady

	_mode = newMode;
}

void RFM69::sleep() {
  setMode(RF69_MODE_SLEEP);
}

void RFM69::setAddress(byte addr)
{
  _address = addr; //& 0x7F; //make sure MSB is reserved for ACK flagging
	writeReg(REG_NODEADRS, _address);
}

// power: 0=min, 31=max
void RFM69::setPowerLevel(byte powerLevel)
{
  _powerLevel = powerLevel;
	writeReg(REG_PALEVEL, readReg(REG_PALEVEL) & (_powerLevel > 31 ? 31 : _powerLevel));
}

bool RFM69::canSend()
{
  if (_mode == RF69_MODE_RX && PAYLOADLEN == 0 && readRSSI() < CSMA_LIMIT) //if signal stronger than -100dBm is detected assume channel activity
  {
    setMode(RF69_MODE_STANDBY);
    return true;
  }
  return false;
}

void RFM69::send(byte toAddress, const void* buffer, byte bufferSize, bool requestACK)
{
  while (!canSend()) receiveDone();
  sendFrame(toAddress, buffer, bufferSize, requestACK, false);
}

//to dramatically increase the chance of getting a packet across, call this function instead of send
// and it handles all the ACK requesting/retrying for you :)
// The only twist is that you have to manually listen to ACK requests on the other side and send back the ACKs
// The reason for the semi-automaton is that the lib is ingterrupt driven and
// requires user action to read the received data and decide what to do with it
// rountrips usually take only 5-8ms at 50kbps
bool RFM69::sendWithRetry(byte toAddress, const void* buffer, byte bufferSize, byte attempts, byte retryWaitTime) {
  long sentTime;
  byte howLong=0;
  for (byte i=0; i<attempts; i++)
  {
    send(toAddress, buffer, bufferSize, true);
    sentTime = millis();
    while (millis()-sentTime<retryWaitTime)
    {
      if (ACKReceived(toAddress))
      {
        ////roundtrip millis
        //Serial.print(" ~ms:");Serial.print(millis()-sentTime);
        return true;
      }
    }
    //Serial.print(" RETRY#");Serial.println(i+1);
  }
  return false;
}

/// Should be polled immediately after sending a packet with ACK request
bool RFM69::ACKReceived(byte fromNodeID) {
  if (receiveDone())
    return (SENDERID == fromNodeID || fromNodeID == 0) && ACK_RECEIVED;
  return false;
}

/// Should be called immediately after reception in case sender wants ACK
void RFM69::sendACK(const void* buffer, byte bufferSize) {
  while (!canSend()) receiveDone();
  sendFrame(SENDERID, buffer, bufferSize, false, true);
}

void RFM69::sendFrame(byte toAddress, const void* buffer, byte bufferSize, bool requestACK, bool sendACK)
{
	setMode(RF69_MODE_STANDBY); //turn off receiver to prevent reception while filling fifo
	while ((readReg(REG_IRQFLAGS1) & RF_IRQFLAGS1_MODEREADY) == 0x00); // Wait for ModeReady
  writeReg(REG_DIOMAPPING1, (readReg(REG_DIOMAPPING1) & 0x3F) | RF_DIOMAPPING1_DIO0_00); // DIO0 is "Packet Sent"
  writeReg(REG_FIFOTHRESH, (readReg(REG_FIFOTHRESH) & 0x7F) | RF_FIFOTHRESH_TXSTART_FIFONOTEMPTY);

  if (bufferSize > MAX_DATA_LEN) bufferSize = MAX_DATA_LEN;

	//write to FIFO
	select();
	SPI.transfer(REG_FIFO | 0x80);
	SPI.transfer(bufferSize + 3);
	SPI.transfer(toAddress);
  SPI.transfer(_address);
  
  //control byte
  if (sendACK)
    SPI.transfer(0x80);
  else if (requestACK)
    SPI.transfer(0x40);
  else SPI.transfer(0x00);
  
	for (byte i = 0; i < bufferSize; i++)
    SPI.transfer(((byte*)buffer)[i]);
	unselect();

	/* no need to wait for transmit mode to be ready since its handled by the radio */
	setMode(RF69_MODE_TX);
	// wait for IRQ when finished transmitting
	while (digitalRead(_interruptPin) == 0); //wait for DIO0 to turn HIGH signalling transmission finish
  setMode(RF69_MODE_STANDBY);
}

void RFM69::interruptHandler() {
  //Serial.print("MODE:");Serial.print(readReg(REG_OPMODE), BIN);
  //Serial.print("\n IRQ1:");Serial.print(readReg(REG_IRQFLAGS1), BIN);
  //Serial.print(" IRQ2:");Serial.print(readReg(REG_IRQFLAGS2), BIN);
  if (_mode == RF69_MODE_RX && (readReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_PAYLOADREADY))
  {
    //Serial.print("\nRDY");
    setMode(RF69_MODE_STANDBY);
    select();
    SPI.transfer(REG_FIFO & 0x7f);
    PAYLOADLEN = SPI.transfer(0);
    PAYLOADLEN = PAYLOADLEN > 66 ? 66 : PAYLOADLEN; //precaution
    TARGETID = SPI.transfer(0);
    if(!(_promiscuousMode || TARGETID==_address || TARGETID==0)) //match this node's address, or broadcast addr 0x0 or anything in promiscuous mode
    {
      PAYLOADLEN = 0;
      unselect();
      return;
    }
		SENDERID = SPI.transfer(0);
    byte CTLbyte = SPI.transfer(0);
    
    ACK_RECEIVED = CTLbyte & 0x80; //extract ACK-requested flag
    ACK_REQUESTED = CTLbyte & 0x40; //extract ACK-received flag
    //Serial.print(" *** ");
    DATALEN = PAYLOADLEN - 3;
    for (byte i= 0; i < DATALEN; i++)
    {
      DATA[i] = SPI.transfer(0);
      //Serial.print((char)DATA[i]);
    }
    //Serial.println(" ***");
    //Serial.println(DATALEN);
    unselect();
    setMode(RF69_MODE_RX);
  }
  //else Serial.println("UNK");
}

void RFM69::isr0() { selfPointer->interruptHandler(); }

void RFM69::receiveBegin() {
  DATALEN = SENDERID = TARGETID = PAYLOADLEN = ACK_REQUESTED = ACK_RECEIVED = 0;
  writeReg(REG_DIOMAPPING1, (readReg(REG_DIOMAPPING1) & 0x3F) | RF_DIOMAPPING1_DIO0_01); //set DIO0 to "PAYLOADREADY" in receive mode
  setMode(RF69_MODE_RX);
}

bool RFM69::receiveDone() {
  noInterrupts(); //will be re-enabled in the setMode() method
  if (_mode == RF69_MODE_RX && PAYLOADLEN>0)
  {
    setMode(RF69_MODE_STANDBY);
    return true;
  }

  receiveBegin();
  return false;
}

// To enable encryption: radio.encrypt("ABCDEFGHIJKLMNOP");
// To disable encryption: radio.encrypt(null) or radio.encrypt(0)
// KEY HAS TO BE 16 bytes !!!
void RFM69::encrypt(const char* key) {
  setMode(RF69_MODE_STANDBY);
  if (key!=0)
  {
    select();
    SPI.transfer(REG_AESKEY1 | 0x80);
    for (byte i = 0; i<16; i++)
      SPI.transfer(key[i]);
    unselect();
  }
  writeReg(REG_PACKETCONFIG2, (readReg(REG_PACKETCONFIG2) & 0xFE) | (key ? 1 : 0));
}

int RFM69::readRSSI(bool forceTrigger) {
  int rssi = 0;
  if (forceTrigger)
  {
    //RSSI trigger not needed if DAGC is in continuous mode
    writeReg(REG_RSSICONFIG, RF_RSSI_START);
    while ((readReg(REG_RSSICONFIG) & RF_RSSI_DONE) == 0x00); // Wait for RSSI_Ready
  }
  rssi = -readReg(REG_RSSIVALUE);
  rssi >>= 1;
  return rssi;
}

byte RFM69::readReg(byte addr)
{
  select();
  SPI.transfer(addr & 0x7F);
  byte regval = SPI.transfer(0);
  unselect();
  return regval;
}

void RFM69::writeReg(byte addr, byte value)
{
  select();
  SPI.transfer(addr | 0x80);
  byte oldregval = SPI.transfer(value);
  unselect();
}

/// Select the transceiver
void RFM69::select() {
  noInterrupts();
  digitalWrite(_slaveSelectPin, LOW);
}

/// UNselect the transceiver chip
void RFM69::unselect() {
  digitalWrite(_slaveSelectPin, HIGH);
  interrupts();
}

// ON  = disable filtering to capture all frames on network
// OFF = enable node+broadcast filtering to capture only frames sent to this/broadcast address
void RFM69::promiscuous(bool onOff) {
  _promiscuousMode=onOff;
  //writeReg(REG_PACKETCONFIG1, (readReg(REG_PACKETCONFIG1) & 0xF9) | (onOff ? RF_PACKET1_ADRSFILTERING_OFF : RF_PACKET1_ADRSFILTERING_NODEBROADCAST));
}

void RFM69::setHighPower(bool onOff) {
  _isRFM69HW = onOff;
  writeReg(REG_OCP, _isRFM69HW ? RF_OCP_OFF : RF_OCP_ON);
  if (_isRFM69HW) //turning ON
    writeReg(REG_PALEVEL, (readReg(REG_PALEVEL) & 0x1F) | RF_PALEVEL_PA1_ON | RF_PALEVEL_PA2_ON); //enable P1 & P2 amplifier stages
  else
    writeReg(REG_PALEVEL, RF_PALEVEL_PA0_ON | RF_PALEVEL_PA1_OFF | RF_PALEVEL_PA2_OFF | _powerLevel); //enable P0 only
}

void RFM69::setHighPowerRegs(bool onOff) {
  writeReg(REG_TESTPA1, onOff ? 0x5D : 0x55);
  writeReg(REG_TESTPA2, onOff ? 0x7C : 0x70);
}

void RFM69::setCS(byte newSPISlaveSelect) {
  _slaveSelectPin = newSPISlaveSelect;
  pinMode(_slaveSelectPin, OUTPUT);
}

//for debugging
void RFM69::readAllRegs()
{
  byte regVal;
	
  for (byte regAddr = 1; regAddr <= 0x4F; regAddr++)
	{
    select();
    SPI.transfer(regAddr & 0x7f);	// send address + r/w bit
    regVal = SPI.transfer(0);
    unselect();

    Serial.print(regAddr, HEX);
    Serial.print(" - ");
    Serial.print(regVal,HEX);
    Serial.print(" - ");
    Serial.println(regVal,BIN);
	}
  unselect();
}

//// doesnt seem to work as expected
// byte RFM69::readTemp(bool calibrate) {
  // if (calibrate)
  // {
    // writeReg(REG_OSC1, RF_OSC1_RCCAL_START);
    // while ((readReg(REG_OSC1) & RF_OSC1_RCCAL_DONE) == 0x00);
  // }

  // writeReg(REG_TEMP1, RF_TEMP1_MEAS_START);
  // while ((readReg(REG_TEMP1) & RF_TEMP1_MEAS_RUNNING));
  // return readReg(REG_TEMP2);
// }