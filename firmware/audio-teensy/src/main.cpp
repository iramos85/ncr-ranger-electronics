
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
AudioPlaySdWav         sfxPlayer;
AudioPlaySdWav         musicPlayer;
AudioPlaySdWav         ambientPlayer;
AudioMixer4            voiceAmbientMixer;
AudioMixer4            outputMixer;
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

// Voice, ambient, SFX and Radio New Vegas output
AudioConnection patchVoiceAmbient(finalMixer, 0, voiceAmbientMixer, 0);
AudioConnection patchAmbient(ambientPlayer, 0, voiceAmbientMixer, 1);
AudioConnection patch10(voiceAmbientMixer, 0, outputMixer, 0);
AudioConnection patch11(sfxPlayer, 0, outputMixer, 1);
AudioConnection patch12(sfxPlayer, 1, outputMixer, 2);

AudioConnection patchMusic(musicPlayer, 0, outputMixer, 3);

AudioConnection patch13(outputMixer, 0, audioOutput, 0);
AudioConnection patch14(outputMixer, 0, audioOutput, 1);

// ======================================================
// ESP32 COMMAND INTERFACE / SD DIAGNOSTICS
// ======================================================

constexpr uint32_t UART_BAUD = 115200;

const float voiceGains[4] = {
  0.20f,  // Natural voice
  0.16f,  // Digital grit
  0.50f,  // Lower-pitched voice
  0.08f   // Delayed low voice
};


char commandBuffer[64];
size_t commandLength = 0;

bool codecReady = false;
bool sdReady = false;

// Radio New Vegas: all playlist files are 44.1kHz / 16-bit / mono.
constexpr uint8_t MUSIC_TRACK_COUNT = 43;
uint8_t musicTrack = 0;  // Zero-based; NV001.WAV is track 0.
bool musicRequested = false;
uint32_t musicStartedAt = 0;
constexpr float MUSIC_MAX_GAIN = 0.16f;
constexpr uint8_t MUSIC_VOLUME_STEP = 5;

uint8_t musicVolume = 50;

void applyMusicVolume() {
  outputMixer.gain(
    3,
    MUSIC_MAX_GAIN * (musicVolume / 100.0f)
  );
}

void reportMusicState(const char *state) {
  char message[64];
  snprintf(message, sizeof(message), "MUSIC:%s:%u/%u", state,
           unsigned(musicTrack + 1), unsigned(MUSIC_TRACK_COUNT));
  Serial.println(message);
  Serial1.println(message);
}

bool startMusicTrack() {
  if (!sdReady) {
    reportMusicState("SD_NOT_READY");
    musicRequested = false;
    return false;
  }
  char filename[13];
  snprintf(filename, sizeof(filename), "NV%03u.WAV", unsigned(musicTrack + 1));
  if (!SD.exists(filename)) {
    reportMusicState("MISSING");
    musicRequested = false;
    return false;
  }
  musicPlayer.stop();
  if (!musicPlayer.play(filename)) {
    reportMusicState("FAILED");
    musicRequested = false;
    return false;
  }
  musicRequested = true;
  musicStartedAt = millis();
  reportMusicState("PLAYING");
  return true;
}

void handleMusicCommand(const char *command) {
  if (!strcmp(command, "MUSIC:VOLUP")) {
  musicVolume = min(100, (int)musicVolume + MUSIC_VOLUME_STEP);
  applyMusicVolume();
  Serial.printf("MUSIC:VOLUME:%u\n", musicVolume);
  Serial1.printf("MUSIC:VOLUME:%u\n", musicVolume);
  return;
}

if (!strcmp(command, "MUSIC:VOLDOWN")) {
  musicVolume = max(0, (int)musicVolume - MUSIC_VOLUME_STEP);
  applyMusicVolume();
  Serial.printf("MUSIC:VOLUME:%u\n", musicVolume);
  Serial1.printf("MUSIC:VOLUME:%u\n", musicVolume);
  return;
}

if (!strcmp(command, "MUSIC:VOLUME")) {
  Serial.printf("MUSIC:VOLUME:%u\n", musicVolume);
  Serial1.printf("MUSIC:VOLUME:%u\n", musicVolume);
  return;
}
  if (!strcmp(command, "MUSIC:PLAY")) {
    if (musicPlayer.isPaused()) {
      musicPlayer.togglePlayPause();
      musicRequested = true;
      reportMusicState("PLAYING");
    } else if (!musicRequested || !musicPlayer.isPlaying()) {
      startMusicTrack();
    } else reportMusicState("PLAYING");
  } else if (!strcmp(command, "MUSIC:PAUSE")) {
    if (musicRequested && musicPlayer.isPlaying() && !musicPlayer.isPaused()) {
      musicPlayer.togglePlayPause();
      reportMusicState("PAUSED");
    } else reportMusicState("NOT_PLAYING");
  } else if (!strcmp(command, "MUSIC:NEXT") || !strcmp(command, "MUSIC:PREV")) {
    musicTrack = uint8_t((musicTrack + MUSIC_TRACK_COUNT +
        (!strcmp(command, "MUSIC:NEXT") ? 1 : -1)) % MUSIC_TRACK_COUNT);
    startMusicTrack();
  } else if (!strcmp(command, "MUSIC:STOP")) {
    musicPlayer.stop();
    musicRequested = false;
    reportMusicState("STOPPED");
  } else if (!strcmp(command, "MUSIC:STATUS")) {
    reportMusicState(musicPlayer.isPaused() ? "PAUSED" :
      (musicRequested && musicPlayer.isPlaying()) ? "PLAYING" : "STOPPED");
  }
}

void updateMusic() {
  // Avoid advancing in the instant immediately after issuing play().
  if (musicRequested && !musicPlayer.isPaused() &&
      !musicPlayer.isPlaying() && millis() - musicStartedAt >= 250) {
    musicTrack = uint8_t((musicTrack + 1) % MUSIC_TRACK_COUNT);
    startMusicTrack();
  }
}

// ======================================================
// VOICE CONTROL
// ======================================================

void setVoiceEnabled(bool enabled) {
  for (int i = 0; i < 4; i++) {
    finalMixer.gain(
      i,
      enabled ? voiceGains[i] : 0.0f
    );
  }
}

// ======================================================
// DIAGNOSTIC REPORTING
// ======================================================

// Reports to both Mac USB and audio ESP32 UART.
void reportSfxStatus(const char *message) {
  Serial.println(message);
  Serial1.println(message);
}

// ======================================================
// SFX PLAYBACK
// ======================================================

// Short effects replace each other; music and ambient remain independent.
// Files use 8.3 SD-compatible names, 44.1kHz/16-bit mono PCM.
bool ambientRequested = false;
uint32_t ambientStartedAt = 0;

void reportFx(const char *prefix, const char *detail) {
  char msg[64];
  snprintf(msg, sizeof(msg), "%s:%s", prefix, detail);
  reportSfxStatus(msg);
}

void playNamedFx(const char *filename) {
  if (!sdReady) { reportFx("FX", "SD_NOT_READY"); return; }
  if (!SD.exists(filename)) { reportFx("FX", "FILE_MISSING"); return; }
  sfxPlayer.stop();
  if (!sfxPlayer.play(filename)) { reportFx("FX", "PLAY_FAILED"); return; }
  reportFx("FX", filename);
}

void setAmbient(bool enabled) {
  if (!enabled) {
    ambientRequested = false;
    ambientPlayer.stop();
    reportFx("AMBIENT", "OFF");
    return;
  }
  if (!sdReady || !SD.exists("AMBIENT.WAV")) {
    reportFx("AMBIENT", "FILE_MISSING");
    return;
  }
  if (ambientRequested) { reportFx("AMBIENT", "ON"); return; }
  ambientRequested = true;
  ambientStartedAt = millis();
  if (!ambientPlayer.play("AMBIENT.WAV")) {
    ambientRequested = false;
    reportFx("AMBIENT", "PLAY_FAILED");
  } else reportFx("AMBIENT", "ON");
}

void updateAmbient() {
  if (ambientRequested && !ambientPlayer.isPlaying() &&
      millis() - ambientStartedAt >= 250) {
    ambientStartedAt = millis();
    if (!ambientPlayer.play("AMBIENT.WAV")) {
      ambientRequested = false;
      reportFx("AMBIENT", "PLAY_FAILED");
    }
  }
}

void handleFxCommand(const char *command) {
  if (!strcmp(command, "SFX_TEST") || !strcmp(command, "FX:READY"))
    playNamedFx("READY.WAV");
  else if (!strcmp(command, "FX:BOOT")) playNamedFx("BOOT.WAV");
  else if (!strcmp(command, "FX:TRACKON")) playNamedFx("TRACKON.WAV");
  else if (!strcmp(command, "FX:TRACKOFF")) playNamedFx("TRACKOFF.WAV");
  else if (!strcmp(command, "FX:CHARGE")) playNamedFx("CHARGE.WAV");
  else if (!strcmp(command, "FX:PLASMA")) playNamedFx("PLASMA.WAV");
  else if (!strcmp(command, "FX:COOL")) playNamedFx("COOL.WAV");
  else if (!strcmp(command, "FX:WARN")) playNamedFx("WARN.WAV");
  else if (!strcmp(command, "FX:RADIO")) playNamedFx("RADIO.WAV");
  else if (!strcmp(command, "FX:SELECT")) playNamedFx("SELECT.WAV");
  else if (!strcmp(command, "FX:STOP")) {
    sfxPlayer.stop();
    reportFx("FX", "STOPPED");
  }
  else if (!strcmp(command, "AMBIENT:ON")) setAmbient(true);
  else if (!strcmp(command, "AMBIENT:OFF")) setAmbient(false);
  else if (!strcmp(command, "AMBIENT:TOGGLE")) setAmbient(!ambientRequested);
  else if (!strcmp(command, "AMBIENT:STATUS")) reportFx("AMBIENT", ambientRequested ? "ON" : "OFF");
  else reportFx("FX", "UNKNOWN_COMMAND");
}

// ======================================================
// ESP32 COMMAND HANDLER
// ======================================================

void handleCommand(const char* command) {

  if (strcmp(command, "PING") == 0) {
    Serial1.println("AUDIO_READY");

  } else if (strcmp(command, "STATUS") == 0) {
    Serial1.println(
      codecReady
        ? "RANGER_VOICE_3C_READY"
        : "AUDIO_CODEC_FAILED"
    );

  } else if (strcmp(command, "VOICE_ON") == 0) {
    setVoiceEnabled(true);
    Serial1.println("VOICE_ON_OK");

  } else if (strcmp(command, "VOICE_OFF") == 0) {
    setVoiceEnabled(false);
    Serial1.println("VOICE_OFF_OK");

  } else if (strncmp(command, "MUSIC:", 6) == 0) {
    handleMusicCommand(command);

  } else if (strcmp(command, "SFX_TEST") == 0 ||
             strncmp(command, "FX:", 3) == 0 ||
             strncmp(command, "AMBIENT:", 8) == 0) {
    handleFxCommand(command);

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

  AudioMemory(160);

  // --------------------------------------------------
  // SD CARD
  // --------------------------------------------------

  sdReady = SD.begin(BUILTIN_SDCARD);

  Serial.print("SD CARD: ");
  Serial.println(
    sdReady ? "READY" : "FAILED"
  );

  // --------------------------------------------------
  // OUTPUT MIXER
  // --------------------------------------------------

  voiceAmbientMixer.gain(0, 1.0f); // Preserve existing voice path
  voiceAmbientMixer.gain(1, 0.35f);
  voiceAmbientMixer.gain(2, 0.0f);
  voiceAmbientMixer.gain(3, 0.0f);
  outputMixer.gain(0, 0.85f); // Voice + ambient
  outputMixer.gain(1, 0.45f); // SFX left
  outputMixer.gain(2, 0.45f); // SFX right
  applyMusicVolume(); //Radio New Vegas

  // --------------------------------------------------
  // AUDIO CODEC
  // --------------------------------------------------

  codecReady = audioShield.enable();

  if (codecReady) {
    audioShield.muteLineout();
    audioShield.lineOutLevel(29);
  }

  Serial.print("SGTL5000: ");
  Serial.println(
    codecReady ? "OK" : "FAILED"
  );

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

  // Lower voice
  pitchShift.setSpeed(0.84);

  // Slightly separate the doubled low voice
  pitchDelay.delay(0, 18);

  // --------------------------------------------------
  // VOICE MIX
  // --------------------------------------------------

  setVoiceEnabled(true);

  // --------------------------------------------------
  // OUTPUT
  // --------------------------------------------------

  if (codecReady) {
    audioShield.unmuteLineout();
  }

  Serial.println(
    codecReady
      ? "RANGER VOICE 3c ACTIVE"
      : "RANGER VOICE 3c CODEC ERROR"
  );
}

// ======================================================
// LOOP
// ======================================================

void loop() {

  updateMusic();
  updateAmbient();

  // --------------------------------------------------
  // ESP32 UART COMMANDS
  // --------------------------------------------------

  while (Serial1.available()) {

    char c = static_cast<char>(Serial1.read());

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

  // --------------------------------------------------
  // USB SERIAL DIAGNOSTIC
  // --------------------------------------------------

  if (Serial.available()) {

    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command == "SFX_TEST" || command.startsWith("FX:") ||
        command.startsWith("AMBIENT:")) {
      handleFxCommand(command.c_str());
    } else if (command.startsWith("MUSIC:")) {
      handleMusicCommand(command.c_str());
    }
  }
}
