#include <cstring>
#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// ======================================================
// GRANULAR MEMORY
// ======================================================

int16_t granularMemory[8192];

// ======================================================
// AUDIO OBJECTS
// ======================================================

AudioInputI2S          audioInput;

AudioFilterBiquad      highPass;
AudioFilterBiquad      lowPass;

AudioEffectBitcrusher  bitcrusher;

AudioEffectGranular    pitchShift;
AudioEffectDelay       pitchDelay;

AudioMixer4            finalMixer;

AudioOutputI2S         audioOutput;
AudioControlSGTL5000   audioShield;


// ======================================================
// ROUTING
// ======================================================

// Mic input
AudioConnection patch1(audioInput, 0, highPass, 0);
AudioConnection patch2(highPass, 0, lowPass, 0);

// Main radio voice
AudioConnection patch3(lowPass, 0, finalMixer, 0);

// Grit layer
AudioConnection patch4(lowPass, 0, bitcrusher, 0);
AudioConnection patch5(bitcrusher, 0, finalMixer, 1);

// Lower-pitched disguise layer
AudioConnection patch6(lowPass, 0, pitchShift, 0);
AudioConnection patch7(pitchShift, 0, finalMixer, 2);

// Tiny delayed copy of lower voice
AudioConnection patch8(pitchShift, 0, pitchDelay, 0);
AudioConnection patch9(pitchDelay, 0, finalMixer, 3);

// Output
AudioConnection patch10(finalMixer, 0, audioOutput, 0);
AudioConnection patch11(finalMixer, 0, audioOutput, 1);

// ======================================================
// ESP32 COMMAND INTERFACE
// ======================================================

constexpr uint32_t UART_BAUD = 115200;

const float voiceGains[4] = {0.45, 0.15, 0.30, 0.08};

char commandBuffer[64];
size_t commandLength = 0;

void setVoiceEnabled(bool enabled) {
  for (int i = 0; i < 4; i++) {
    finalMixer.gain(i, enabled ? voiceGains[i] : 0.0f);
  }
}

bool codecReady = false;

void handleCommand(const char* command) {
  if (strcmp(command, "PING") == 0) {
    Serial1.println("AUDIO_READY");

 } else if (strcmp(command, "STATUS") == 0) {
  Serial1.println(codecReady
    ? "RANGER_VOICE_3C_READY"
    : "AUDIO_CODEC_FAILED");

  } else if (strcmp(command, "VOICE_ON") == 0) {
    setVoiceEnabled(true);
    Serial1.println("VOICE_ON_OK");

  } else if (strcmp(command, "VOICE_OFF") == 0) {
    setVoiceEnabled(false);
    Serial1.println("VOICE_OFF_OK");

  } else if (strcmp(command, "SFX_TEST") == 0) {
    Serial1.println("SFX_NOT_CONFIGURED");

  } else {
    Serial1.println("UNKNOWN_COMMAND");
  }
}

// ======================================================
// SETUP
// ======================================================

void setup() {
  Serial.begin(115200);
  Serial1.begin(UART_BAUD);
  delay(1000);

  AudioMemory(120);

  codecReady = audioShield.enable();

if (codecReady) {
  audioShield.muteLineout();
  audioShield.lineOutLevel(29);
}

  Serial.print("SGTL5000: ");
  Serial.println(codecReady ? "OK" : "FAILED");

  // --------------------------------------------------
  // INPUT
  // --------------------------------------------------

  audioShield.inputSelect(AUDIO_INPUT_LINEIN);
  audioShield.lineInLevel(0);


  // --------------------------------------------------
  // RADIO SHAPING
  // --------------------------------------------------

highPass.setHighpass(0, 180, 0.707);
lowPass.setLowpass(0, 3800, 0.707);


  // --------------------------------------------------
  // DIGITAL GRIT
  // --------------------------------------------------

bitcrusher.bits(11);
bitcrusher.sampleRate(14000);


  // --------------------------------------------------
  // PITCH DISGUISE
  // --------------------------------------------------

  pitchShift.begin(granularMemory, 8192);

  // Short grains keep latency reasonable
  pitchShift.beginPitchShift(25);

  // 0.84 = roughly 3 semitones lower
  pitchShift.setSpeed(0.84);

  // Slightly separate the doubled low voice
  pitchDelay.delay(0, 18);


  // --------------------------------------------------
  // MIX
  // --------------------------------------------------

// Recognizable/intelligible core
finalMixer.gain(0, 0.45);

// Radio grit
finalMixer.gain(1, 0.15);

// Lower disguise voice
finalMixer.gain(2, 0.30);

// Quiet delayed lower voice
finalMixer.gain(3, 0.08);

// --------------------------------------------------
// OUTPUT
// --------------------------------------------------

if (codecReady) {
  audioShield.unmuteLineout();
}

Serial.println(codecReady
  ? "RANGER VOICE 3c ACTIVE"
  : "RANGER VOICE 3c CODEC ERROR");
}

void loop() {
  while (Serial1.available()) {
    char c = Serial1.read();

    if (c == '\n') {
      commandBuffer[commandLength] = '\0';

      if (commandLength > 0) {
        handleCommand(commandBuffer);
      }

      commandLength = 0;

    } else if (c != '\r') {
      if (commandLength < sizeof(commandBuffer) - 1) {
        commandBuffer[commandLength++] = c;
      } else {
        commandLength = 0;
      }
    }
  }
}