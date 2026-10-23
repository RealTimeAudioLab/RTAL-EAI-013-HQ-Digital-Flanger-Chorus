/*
 * RTAL HQ FLANGER V1.0 FINAL
 * Arduino IDE 1.8.19 / ESP32 Core 2.0.16; 32kHz stereo 32-bit I2S/128.
 */

#include <Arduino.h>
// 0 = normal operation (no periodic/parameter-change DSP telemetry)
// 1 = development diagnostics (full DSP telemetry)
#ifndef RTAL_FLANGER_DEBUG_TELEMETRY
#define RTAL_FLANGER_DEBUG_TELEMETRY 0
#endif
#include <SPI.h>
#include <SD.h>
#include <U8g2lib.h>
#include <math.h>
#include "src/ClusterFluxAudio.h"
#include "src/FluxInput.h"
#include "src/FluxMidiClock.h"
#include "FluxPreset.h"
#include "MorphSnapshot.h"
#include "src/FlangerSplash.h"

#define SD_CS_PIN 9
#define OLED_SCK  12
#define OLED_MOSI 11
#define OLED_CS   10
#define OLED_DC    6
#define OLED_RST   7

static const uint8_t BTN_PINS[8] = {21,47,45,38,4,15,3,14};
#define ENC_A_PIN    1
#define ENC_B_PIN    2
#define ENC_SW_PIN  42

#define I2S_BCLK_PIN 18
#define I2S_LRCK_PIN 16
#define I2S_DOUT_PIN 17
#define I2S_DIN_PIN   5
#define MIDI_DIN_RX  40
#define MIDI_DIN_TX  39

static U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R0, OLED_CS, OLED_DC, OLED_RST);
static ClusterFluxAudio audio(I2S_BCLK_PIN,I2S_LRCK_PIN,I2S_DOUT_PIN,I2S_DIN_PIN);
static FluxInput input(BTN_PINS, ENC_A_PIN,ENC_B_PIN,ENC_SW_PIN);
static FluxMidiClock midiClock;
// Display-only 24-PPQN BPM estimator; never feeds the LFO clock.
static uint32_t extBpmLastTickUs=0, extBpmWindowStartUs=0;
static uint8_t extBpmWindowTicks=0;
static float extBpmFiltered=0.0f;
static unsigned extBpmShown=0;
static uint32_t extBpmLastDisplayMs=0;
static bool extBpmValid=false;
static void updateExtBpmDisplayTick(uint32_t tUs) {
  if(extBpmLastTickUs==0) {extBpmLastTickUs=extBpmWindowStartUs=tUs;return;}
  const uint32_t dt=tUs-extBpmLastTickUs;
  if(dt<5000U)return;
  extBpmLastTickUs=tUs;
  if(dt>125000U) {extBpmWindowTicks=0;extBpmWindowStartUs=tUs;extBpmValid=false;return;}
  if(++extBpmWindowTicks>=24U) {
    const uint32_t elapsed=tUs-extBpmWindowStartUs;
    extBpmWindowStartUs=tUs;extBpmWindowTicks=0;
    if(elapsed==0)return;
    const float measured=60000000.0f/(float)elapsed;
    if(measured<20.0f || measured>300.0f)return;
    if(!extBpmValid){extBpmFiltered=measured;extBpmShown=(unsigned)(measured+0.5f);extBpmValid=true;}
    else extBpmFiltered+=0.20f*(measured-extBpmFiltered);
  }
}
static void refreshExtBpmShown(uint32_t nowMs) {
  if(nowMs-extBpmLastDisplayMs<500U)return;
  extBpmLastDisplayMs=nowMs;
  if(!extBpmValid)return;
  const unsigned target=(unsigned)(extBpmFiltered+0.5f);
  if(target>extBpmShown+1U || target+1U<extBpmShown)extBpmShown=target;
}


enum { TIME=0, FEEDBACK, LFO_RATE, LFO_AMOUNT, MIX, DRIVE, OUTPUT_LEVEL,
       BBD_TONE, BBD_SAT, LFO_WAVE, LFO_SMOOTH, SYNC_ENABLE, SYNC_DIV,
       INT_BPM, RESET_START, BBD_MODEL, FB_COLOR, BBD_NOISE,
       STEREO_WIDTH, STEREO_PHASE, MODE_PARAM, BYPASS_PARAM, STEREO_MODE_PARAM, INPUT_PARAM, PARAM_COUNT };


static const char * const LABELS[PARAM_COUNT] = {
  "TIME", "FEEDBACK", "LFO RATE", "LFO AMOUNT", "MIX", "DRIVE", "OUTPUT",
  "BBD TONE", "BBD SAT", "LFO WAVE", "LFO SMOOTH", "SYNC", "SYNC DIV",
  "INT BPM", "START RESET", "BBD MODEL", "FB COLOR", "BBD NOISE",
  "ST WIDTH", "ST PHASE", "MODE", "BYPASS", "STEREO MODE", "INPUT"
};
static const char * const WAVE_NAMES[FLUX_WAVE_COUNT] = {
  "SINE", "TRIANGLE", "SQUARE", "SAW UP", "SAW DOWN", "RANDOM", "SMOOTH RAND"
};

// Quarter-note beats per complete LFO cycle, assuming 4/4 for bars.
// Triplet/dotted entries are deliberate rhythmic extensions.
static const char * const DIV_NAMES[] = {
  "4BAR", "2BAR", "1BAR", "1/2", "1/4", "1/4T",
  "1/8", "1/8D", "1/8T", "1/16", "1/16T", "1/32"
};
static const float DIV_BEATS[] = {
  16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 2.0f/3.0f,
  0.5f, 0.75f, 1.0f/3.0f, 0.25f, 1.0f/6.0f, 0.125f
};
static const uint8_t DIV_COUNT = sizeof(DIV_BEATS)/sizeof(DIV_BEATS[0]);

// Preserve F002 defaults and free-running SINE by default.
// Additional modes: wave 0..6; sync 0/1; division 0..11; bpm 40..240.
static uint8_t value[PARAM_COUNT] = {
  50, 50, 35, 25, 50, 20, 50, 50, 30, 0, 55, 0, 4, 120, 1,
  0, 0, 0, 100, 50, 0, 0, 0, 0
};
// F009A advanced parameters, separate from the legacy 24-byte preset layout.
enum { A_RATE=0, A_DEPTH, A_WAVE, A_TARGET, A_CROSS, A_CHARACTER, A_DUAL, A_ENV, A_DAMP, A_MOTION, A_TZ, A_MATRIX, A_ENV_ATTACK, A_ENV_RELEASE, A_ENV_TARGET, ADV_COUNT };
static_assert(PARAM_COUNT == MORPH_LEGACY_COUNT && ADV_COUNT == MORPH_ADV_COUNT, "MorphSnapshot layout must match preset parameter counts");
static const uint8_t ADV_MIDI_CC[ADV_COUNT]={26,27,24,31,107,87,108,116,117,88,14,15,20,53,104};
static uint8_t adv[ADV_COUNT]={35,0,0,0,0,0,0,0,0,0,0,0,20,50,0};
static uint8_t advMax(uint8_t p){return p==A_WAVE||p==A_TARGET?3:p==A_CHARACTER?2:p==A_MATRIX?3:p==A_ENV_TARGET?3:100;}
static void applyAdvanced(uint8_t p){
 switch(p){
 case A_RATE: audio.setLfo2Rate(adv[p]);break;
 case A_DEPTH: audio.setLfo2Depth(adv[p]);break;
 case A_WAVE: audio.setLfo2Wave(adv[p]);break;
 case A_TARGET: audio.setLfo2Target(adv[p]);break;
 case A_CROSS: audio.setCrossFeedback(adv[p]);break;
 case A_CHARACTER: audio.setCharacter(adv[p]);break;
 case A_DUAL: audio.setDualTap(adv[p]);break;
 case A_ENV: audio.setEnvelope(adv[p]);break;
 case A_DAMP: audio.setFeedbackDamp(adv[p]);break;
 case A_MOTION: audio.setStereoMotion(adv[p]);break;
 case A_TZ: audio.setThroughZero(adv[p]);break;
 case A_MATRIX: audio.setFeedbackMatrix(adv[p]);break;
 case A_ENV_ATTACK: audio.setEnvAttack(adv[p]);break;
 case A_ENV_RELEASE: audio.setEnvRelease(adv[p]);break;
 case A_ENV_TARGET: audio.setEnvTarget(adv[p]);break;
 }
}
static void applyAllAdvanced(){for(uint8_t p=0;p<ADV_COUNT;++p)applyAdvanced(p);}
static uint8_t selected = TIME;
static uint32_t lastEncoderMoveMs = 0;
static uint8_t encoderBurst = 0;
static int16_t acceleratedEncoderStep(int16_t step, uint8_t param) {
  if (!step) return 0;
  const uint32_t now = millis();
  if (now - lastEncoderMoveMs < 85U) {
    if (encoderBurst < 12) ++encoderBurst;
  } else encoderBurst = 0;
  lastEncoderMoveMs = now;
  // Discrete controls must never skip a selection.
  if (param == LFO_WAVE || param == SYNC_ENABLE || param == SYNC_DIV ||
      param == RESET_START || param == MODE_PARAM || param == BYPASS_PARAM ||
      param == STEREO_MODE_PARAM || param == INPUT_PARAM) return step;
  const int multiplier = encoderBurst >= 7 ? 4 : (encoderBurst >= 3 ? 2 : 1);
  return (int16_t)(step * multiplier);
}

// FIX11: Home-view selection is stored separately from preset and MIDI formats.
static bool sdReady=false;
static bool motionHome=false;
static const char *HOME_VIEW_PATH="/RTAL_FLANGER/HOME_VIEW.CFG";
static uint8_t motionTrace[112];
static uint8_t motionHead=0;
static bool motionTraceReady=false;
static void loadHomeView(){
  if(!sdReady)return;
  File f=SD.open(HOME_VIEW_PATH,FILE_READ);
  if(f){int v=f.read();motionHome=(v=='1');f.close();}
}
static void saveHomeView(){
  if(!sdReady)return;
  File f=SD.open(HOME_VIEW_PATH,FILE_WRITE);
  if(f){f.seek(0);f.write((uint8_t)(motionHome?'1':'0'));f.close();}
}
static uint8_t activePreset=0; // absolute slot 1..128
static uint8_t presetBank=0;   // 0..15, selected bank for T1-T8
static uint8_t bankFileSlot(uint8_t key){return (uint8_t)(presetBank*8U+key);}
static char notice[26]={0};
static uint32_t noticeUntil=0;
static bool suppressMidiPresetNotice=false;
static void showNotice(const char *s) {
  // A MIDI Program Change burst should not obscure the performance UI.
  if(suppressMidiPresetNotice && strstr(s," LOADED"))return;
  snprintf(notice,sizeof(notice),"%s",s);
  noticeUntil=millis()+1500U;
}
// FIX10: Draw on the already-rendered 128x64 framebuffer, before sendBuffer().
// The whole central region is opaque, so underlying pixels cannot bleed through.
static void drawSdActionOverlay(){
  if((int32_t)(noticeUntil-millis())<=0)return;
  const bool error = strstr(notice,"ERROR") || strstr(notice,"INVALID") ||
                     strstr(notice,"EMPTY") || strstr(notice,"NOT READY");
  u8g2.setDrawColor(0);
  u8g2.drawBox(5,18,118,30);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(5,18,118,30);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(12,29,error?"! SD / PRESET !":"SD / PRESET");
  // Center and clip the message to the overlay's 112px text area.
  char display[24];snprintf(display,sizeof(display),"%.22s",notice);
  while(u8g2.getStrWidth(display)>108 && strlen(display)>0)display[strlen(display)-1]=0;
  const int x=64-(int)u8g2.getStrWidth(display)/2;
  u8g2.drawStr(x,41,display);
}
// FIX12: synchronous SD operations need an explicit framebuffer transfer before I/O.
// The busy message persists until replaced by the final 1.5s result notice.
static void sdProgress(const char *message){
  snprintf(notice,sizeof(notice),"%s",message);
  noticeUntil=millis()+600000UL;
  u8g2.clearBuffer();
  drawSdActionOverlay();
  u8g2.sendBuffer();
}
static void sdProfile(const char *phase,uint32_t start){
  Serial.printf("SD SAVE: %-20s %lu ms\n",phase,(unsigned long)(millis()-start));
}
// Fixed preset number; only the name moves, clipped to its own header area.
static void drawScrollingPresetHeader(const char *name){
  char prefix[9];snprintf(prefix,sizeof(prefix),"P%03u ",(unsigned)activePreset);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0,7,prefix);
  const int left=u8g2.getStrWidth(prefix), width=128-left;
  const int pixels=u8g2.getStrWidth(name);
  if(pixels<=width){u8g2.drawStr(left,7,name);return;}
  // Pause 1s at start, travel at 24px/s, pause 1.5s at end.
  const uint32_t travel=(uint32_t)(pixels-width)*1000UL/24UL;
  const uint32_t cycle=1000UL+travel+1500UL;
  const uint32_t t=millis()%cycle;
  int offset=0;
  if(t>1000UL)offset=(int)(((t-1000UL)*24UL)/1000UL);
  if(offset>pixels-width)offset=pixels-width;
  u8g2.setClipWindow(left,0,128,9);
  u8g2.drawStr(left-offset,7,name);
  u8g2.setMaxClipWindow();
}
static bool chorus = false, bypass = false, enhanced = false;
static uint8_t inputSource = 0; // 0=LEFT 1=RIGHT 2=SUM
static bool clockExternal = false;
static uint32_t lastDraw = 0, lastTelemetry = 0, lastRateUpdate = 0;

static uint8_t paramMin(uint8_t p) { return p == INT_BPM ? 40 : 0; }
static uint8_t paramMax(uint8_t p) {
  if (p == LFO_WAVE) return FLUX_WAVE_COUNT - 1;
  if (p == SYNC_ENABLE || p == RESET_START) return 1;
  if (p == SYNC_DIV) return DIV_COUNT - 1;
  if (p == INT_BPM) return 240;
  if (p == MODE_PARAM || p == BYPASS_PARAM || p == STEREO_MODE_PARAM) return 1;
  if (p == INPUT_PARAM) return 2;
  return 100;
}

static void applyValue(uint8_t p) {
  switch (p) {
    case TIME:          audio.setTime(value[p]); break;
    case FEEDBACK:      audio.setFeedback(value[p]); break;
    case LFO_RATE:      audio.setRate(value[p]); break;
    case LFO_AMOUNT:    audio.setAmount(value[p]); break;
    case MIX:           audio.setMix(value[p]); break;
    case DRIVE:         audio.setDrive(value[p]); break;
    case OUTPUT_LEVEL:  audio.setOutput(value[p]); break;
    case BBD_TONE:      audio.setTone(value[p]); break;
    case BBD_SAT:       audio.setSaturation(value[p]); break;
    case LFO_WAVE:      audio.setWaveform(value[p]); break;
    case LFO_SMOOTH:    audio.setWaveSmooth(value[p]); break;
    case SYNC_ENABLE:   audio.setSyncMode(value[p] != 0); break;
    case SYNC_DIV:      break; // used by updateSyncRate() below
    case INT_BPM:       break;
    case RESET_START:   break;
    case BBD_MODEL:     audio.setBbdModel(value[p]); break;
    case FB_COLOR:      audio.setFeedbackColor(value[p]); break;
    case BBD_NOISE:     audio.setBbdNoise(value[p]); break;
    case STEREO_WIDTH:  audio.setStereoWidth(value[p]); break;
    case STEREO_PHASE:  audio.setStereoPhase(value[p]); break;
    case MODE_PARAM: chorus=value[p]!=0; audio.setChorus(chorus); break;
    case BYPASS_PARAM: bypass=value[p]!=0; audio.setBypass(bypass); break;
    case STEREO_MODE_PARAM: enhanced=value[p]!=0; audio.setEnhanced(enhanced); break;
    case INPUT_PARAM: inputSource=value[p]; audio.setInputSource(inputSource); break;
  }
}


// Preset type declared above before Arduino-generated prototypes.
static_assert(sizeof(FluxPreset) == 36, "Preset binary layout changed");
static_assert(PARAM_COUNT == 24, "Update FluxPreset when changing parameters");
static uint32_t checksumPreset(const FluxPreset &p) {
  const uint8_t *b=(const uint8_t*)&p; uint32_t h=2166136261UL;
  for(size_t i=0;i<sizeof(p)-sizeof(p.checksum);++i){h^=b[i];h*=16777619UL;} return h;
}
static void presetPath(char *out,size_t n,uint8_t slot){snprintf(out,n,slot<=8?"/RTAL_FLANGER/FLUX%02u.BIN":"/RTAL_FLANGER/FLUX%03u.BIN",(unsigned)slot);}
static void advancedPath(char *out,size_t n,uint8_t slot){snprintf(out,n,slot<=8?"/RTAL_FLANGER/FLUX%02u.ADV":"/RTAL_FLANGER/FLUX%03u.ADV",(unsigned)slot);}
static void saveAdvanced(uint8_t slot){
 if(!sdReady)return;
 char path[48],tmp[48];advancedPath(path,sizeof(path),slot);
 snprintf(tmp,sizeof(tmp),slot<=8?"/RTAL_FLANGER/FLUX%02u.ATMP":"/RTAL_FLANGER/FLUX%03u.ATMP",(unsigned)slot);
 uint8_t blob[4+ADV_COUNT]={};blob[0]='A';blob[1]='D';blob[2]=3;blob[3]=ADV_COUNT;
 for(uint8_t i=0;i<ADV_COUNT;++i)blob[4+i]=adv[i];
 uint32_t t=millis(); SD.remove(tmp); Serial.printf("SD PROFILE: ADV REMOVE TEMP        %lu ms\n",(unsigned long)(millis()-t));
 t=millis(); File f=SD.open(tmp,FILE_WRITE); Serial.printf("SD PROFILE: ADV OPEN TEMP          %lu ms\n",(unsigned long)(millis()-t));
 if(!f){showNotice("ADV SAVE ERROR");return;}
 t=millis(); const bool ok=f.write(blob,sizeof(blob))==sizeof(blob); Serial.printf("SD PROFILE: ADV WRITE 19B          %lu ms\n",(unsigned long)(millis()-t));
 t=millis(); f.close(); Serial.printf("SD PROFILE: ADV CLOSE              %lu ms\n",(unsigned long)(millis()-t));
 if(!ok){SD.remove(tmp);showNotice("ADV SAVE ERROR");return;}
 t=millis(); SD.remove(path); Serial.printf("SD PROFILE: ADV REMOVE OLD         %lu ms\n",(unsigned long)(millis()-t));
 t=millis(); bool renamed=SD.rename(tmp,path); Serial.printf("SD PROFILE: ADV RENAME             %lu ms\n",(unsigned long)(millis()-t));
 if(!renamed)showNotice("ADV SAVE ERROR");
}
static void loadAdvanced(uint8_t slot){
 const uint8_t defaults[ADV_COUNT]={35,0,0,0,0,0,0,0,0,0,0,0,20,50,0};
 memcpy(adv,defaults,ADV_COUNT);
 char path[48];advancedPath(path,sizeof(path),slot);
 File f=SD.open(path,FILE_READ);
 if(f){
   const size_t length=f.size();
   uint8_t blob[4+ADV_COUNT]={};
   const bool ok=(length==10 || length==14 || length==sizeof(blob)) && f.read(blob,length)==length;
   f.close();
   if(ok && blob[0]=='A' && blob[1]=='D' &&
      ((blob[2]==1 && blob[3]==6 && length==10) ||
       (blob[2]==2 && blob[3]==10 && length==14) ||
       (blob[2]==3 && blob[3]==ADV_COUNT && length==sizeof(blob)))){
     const uint8_t count=blob[3];
     bool valid=true;
     for(uint8_t i=0;i<count;++i)if(blob[4+i]>advMax(i))valid=false;
     if(valid)memcpy(adv,blob+4,count);
   }
 }
 applyAllAdvanced();
}
static bool morphConfigSave(uint8_t slot);
static void morphConfigLoad(uint8_t slot);
static bool morphConfigExists(uint8_t slot);
static bool morphConfigFlags[128]={};
static bool morphConfigFlagsReady=false;
// Explicitly edited morph configuration; never infer this from neutral defaults.
static bool morphConfigDirty=false;
// FIX5A: compact persistent morph bitmap, validated by magic/version/checksum.
static const char* MORPH_INDEX_PATH="/RTAL_FLANGER/MORPH_INDEX.DAT";
static bool morphIndexWrite(){
  if(!sdReady)return false;
  uint8_t data[21]={ 'M','I',1,128 };
  for(uint8_t i=0;i<128;++i)if(morphConfigFlags[i])data[4+i/8]|=(uint8_t)(1U<<(i%8));
  uint8_t check=0;for(uint8_t i=0;i<20;++i)check^=data[i];data[20]=check;
  const char* tmp="/RTAL_FLANGER/MORPH_INDEX.TMP";
  SD.remove(tmp);File f=SD.open(tmp,FILE_WRITE);
  if(!f){Serial.println("MORPH INDEX: WRITE OPEN FAILED");return false;}
  bool ok=f.write(data,sizeof(data))==sizeof(data);f.flush();f.close();
  if(!ok){SD.remove(tmp);Serial.println("MORPH INDEX: WRITE FAILED");return false;}
  SD.remove(MORPH_INDEX_PATH);
  if(!SD.rename(tmp,MORPH_INDEX_PATH)){Serial.println("MORPH INDEX: RENAME FAILED");return false;}
  Serial.println("MORPH INDEX: SAVED");return true;
}
static void morphConfigFlagsLoad(){
  if(morphConfigFlagsReady)return;
  const uint32_t start=millis();
  if(sdReady){
    File f=SD.open(MORPH_INDEX_PATH,FILE_READ);
    if(f){
      uint8_t data[21]={};bool ok=f.size()==sizeof(data)&&f.read(data,sizeof(data))==sizeof(data);f.close();
      uint8_t check=0;for(uint8_t i=0;i<20;++i)check^=data[i];
      if(ok&&data[0]=='M'&&data[1]=='I'&&data[2]==1&&data[3]==128&&check==data[20]){
        for(uint8_t i=0;i<128;++i)morphConfigFlags[i]=(data[4+i/8]&(1U<<(i%8)))!=0;
        morphConfigFlagsReady=true;
        Serial.printf("MORPH INDEX: LOADED in %lu ms\n",(unsigned long)(millis()-start));return;
      }
      Serial.println("MORPH INDEX: INVALID; REBUILDING");
    }else Serial.println("MORPH INDEX: MISSING; REBUILDING");
  }
  for(uint16_t i=1;i<=128;++i)morphConfigFlags[i-1]=morphConfigExists((uint8_t)i);
  morphConfigFlagsReady=true;
  Serial.printf("MORPH INDEX: SCANNED 128 in %lu ms\n",(unsigned long)(millis()-start));
  morphIndexWrite();
}
static bool morphConfigCached(uint8_t slot){
  if(slot<1||slot>128)return false;
  return morphConfigFlagsReady ? morphConfigFlags[slot-1] : false;
}

// RC3 FIX7: validated, decoded preset data in internal RAM (SD remains authoritative).
struct CachedPreset {
  uint8_t main[PARAM_COUNT];
  uint8_t advanced[ADV_COUNT];
  uint8_t morph[14];
  bool validMain;
  bool validMorph;
};
// FIX13G FIX1: Morph state must be declared before savePreset() uses it.
static uint8_t morphSlotA=1, morphSlotB=2, morphPosition=0, morphTime=35, morphMode=0, morphCurve=0;
static bool morphPingPong=false, morphReverse=false, morphSync=false, morphRunning=false;
static uint8_t morphDivision=4, morphFavorite=0;
static CachedPreset presetRam[128];
static bool presetRamReady=false;
static const uint8_t presetAdvDefaults[ADV_COUNT]={35,0,0,0,0,0,0,0,0,0,0,0,20,50,0};
static bool presetRamReadSlot(uint8_t slot){
  if(!sdReady||slot<1||slot>128)return false;
  CachedPreset &c=presetRam[slot-1];
  c.validMain=false;c.validMorph=false;
  memcpy(c.advanced,presetAdvDefaults,ADV_COUNT);
  char path[48];presetPath(path,sizeof(path),slot);
  File f=SD.open(path,FILE_READ);
  if(f){
    FluxPreset p={};const bool ok=f.size()==sizeof(p)&&f.read((uint8_t*)&p,sizeof(p))==sizeof(p);f.close();
    if(ok&&p.magic==0x52544658UL&&p.version==1&&p.count==PARAM_COUNT&&p.checksum==checksumPreset(p)){
      bool valid=true;
      for(uint8_t i=0;i<PARAM_COUNT;++i)if(p.params[i]<paramMin(i)||p.params[i]>paramMax(i))valid=false;
      if(valid){memcpy(c.main,p.params,PARAM_COUNT);c.validMain=true;}
    }
  }
  advancedPath(path,sizeof(path),slot);f=SD.open(path,FILE_READ);
  if(f){
    uint8_t blob[4+ADV_COUNT]={};const size_t len=f.size();
    const bool ok=(len==10||len==14||len==sizeof(blob))&&f.read(blob,len)==len;f.close();
    if(ok&&blob[0]=='A'&&blob[1]=='D'&&
       ((blob[2]==1&&blob[3]==6&&len==10)||(blob[2]==2&&blob[3]==10&&len==14)||
        (blob[2]==3&&blob[3]==ADV_COUNT&&len==sizeof(blob)))){
      bool valid=true;for(uint8_t i=0;i<blob[3];++i)if(blob[4+i]>advMax(i))valid=false;
      if(valid)memcpy(c.advanced,blob+4,blob[3]);
    }
  }
  // Missing MORPH files are normal and retain the original neutral defaults.
  if(morphConfigCached(slot)){
    snprintf(path,sizeof(path),"/RTAL_FLANGER/MORPH%03u.CFG",(unsigned)slot);
    f=SD.open(path,FILE_READ);
    if(f){
      uint8_t b[14]={};bool ok=f.size()==sizeof(b)&&f.read(b,sizeof(b))==sizeof(b);f.close();
      uint8_t chk=0;for(uint8_t i=0;i<13;++i)chk^=b[i];
      if(ok&&b[0]=='M'&&b[1]=='F'&&b[2]==1&&chk==b[13]&&
         b[3]>=1&&b[3]<=128&&b[4]>=1&&b[4]<=128&&b[5]<=100&&b[6]<=100&&
         b[7]<=1&&b[8]<=1&&b[9]<=1&&b[10]<=1&&b[11]<=7&&b[12]<=1){
        memcpy(c.morph,b,sizeof(b));c.validMorph=true;
      }
    }
  }
  return c.validMain;
}
// FIX8: Persistent, checksummed snapshot of the validated 128-slot RAM cache.
// SD preset files remain authoritative on first build; cache is updated after saves.
static const char* PRESET_CACHE_PATH="/RTAL_FLANGER/PRESET_CACHE.DAT";
static const char* PRESET_CACHE_TMP="/RTAL_FLANGER/PRESET_CACHE.TMP";
struct PresetCacheHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t slots;
  uint32_t payloadBytes;
  uint32_t checksum;
};
static uint32_t presetCacheChecksum(const uint8_t* data,size_t n){
  uint32_t h=2166136261UL;
  for(size_t i=0;i<n;++i){h^=data[i];h*=16777619UL;}
  return h;
}
static bool presetCacheRead(){
  File f=SD.open(PRESET_CACHE_PATH,FILE_READ);
  if(!f)return false;
  PresetCacheHeader h={};
  bool ok=f.size()==sizeof(h)+sizeof(presetRam) &&
          f.read((uint8_t*)&h,sizeof(h))==sizeof(h) &&
          h.magic==0x38435052UL && h.version==1 && h.slots==128 &&
          h.payloadBytes==sizeof(presetRam);
  if(ok)ok=f.read((uint8_t*)presetRam,sizeof(presetRam))==sizeof(presetRam);
  f.close();
  if(ok)ok=presetCacheChecksum((const uint8_t*)presetRam,sizeof(presetRam))==h.checksum;
  if(!ok)memset(presetRam,0,sizeof(presetRam));
  return ok;
}
static bool presetCacheWrite(){
  if(!sdReady)return false;
  PresetCacheHeader h={0x38435052UL,1,128,(uint32_t)sizeof(presetRam),
                      presetCacheChecksum((const uint8_t*)presetRam,sizeof(presetRam))};
  SD.remove(PRESET_CACHE_TMP);
  File f=SD.open(PRESET_CACHE_TMP,FILE_WRITE);
  if(!f)return false;
  bool ok=f.write((const uint8_t*)&h,sizeof(h))==sizeof(h);
  ok=(f.write((const uint8_t*)presetRam,sizeof(presetRam))==sizeof(presetRam))&&ok;
  f.flush();f.close();
  if(!ok){SD.remove(PRESET_CACHE_TMP);return false;}
  // Preserve the previous cache until the temporary file is completely written.
  SD.remove(PRESET_CACHE_PATH);
  if(!SD.rename(PRESET_CACHE_TMP,PRESET_CACHE_PATH)){
    SD.remove(PRESET_CACHE_TMP);return false;
  }
  return true;
}
// FIX13C: update one cached slot in place, then commit the global checksum.
// A power failure between the two writes leaves an invalid checksum; boot then
// rebuilds the cache from the authoritative individual preset files.
// No on-disk format change (header version 1, 128 slots).
static bool presetCacheUpdateSlot(uint8_t slot){
  if(!sdReady||slot<1||slot>128||!presetRamReady)return false;
  const uint32_t begin=millis();
  File f=SD.open(PRESET_CACHE_PATH,"r+");
  if(!f){Serial.println("SD CACHE: UPDATE OPEN FAILED");return false;}
  PresetCacheHeader existing={};
  bool ok=f.size()==sizeof(PresetCacheHeader)+sizeof(presetRam) &&
          f.read((uint8_t*)&existing,sizeof(existing))==sizeof(existing) &&
          existing.magic==0x38435052UL && existing.version==1 &&
          existing.slots==128 && existing.payloadBytes==sizeof(presetRam);
  if(ok){
    const uint32_t offset=sizeof(PresetCacheHeader)+(uint32_t)(slot-1)*sizeof(CachedPreset);
    ok=f.seek(offset) &&
       f.write((const uint8_t*)&presetRam[slot-1],sizeof(CachedPreset))==sizeof(CachedPreset);
  }
  if(ok){
    const uint32_t checksum=presetCacheChecksum((const uint8_t*)presetRam,sizeof(presetRam));
    const uint32_t checksumOffset=offsetof(PresetCacheHeader,checksum);
    ok=f.seek(checksumOffset) && f.write((const uint8_t*)&checksum,sizeof(checksum))==sizeof(checksum);
  }
  f.flush();f.close();
  Serial.printf("SD PROFILE: CACHE SLOT UPDATE %lu ms (%s)\n",
                (unsigned long)(millis()-begin),ok?"OK":"FAILED");
  return ok;
}
static void drawBootProgress(uint16_t loaded,uint16_t total,const char* phase);
static void presetRamInit(){
  if(!sdReady)return;
  const uint32_t start=millis();unsigned valid=0,morph=0;
  drawBootProgress(0,128,"LOADING PRESET CACHE");
  if(presetCacheRead()){
    presetRamReady=true;
    drawBootProgress(128,128,"PRESET CACHE READY");
    for(unsigned i=0;i<128;++i){if(presetRam[i].validMain)++valid;if(presetRam[i].validMorph)++morph;}
    Serial.printf("PRESET CACHE: LOADED valid=%u morph=%u bytes=%u in %lu ms\n",valid,morph,
                  (unsigned)sizeof(presetRam),(unsigned long)(millis()-start));
    return;
  }
  drawBootProgress(0,128,"BUILDING PRESET CACHE");
  Serial.println("PRESET CACHE: MISSING OR INVALID; REBUILDING");
  Serial.printf("PRESET RAM: BUILDING 128 slots (%u bytes)\n",(unsigned)sizeof(presetRam));
  for(uint16_t i=1;i<=128;++i){
    if(presetRamReadSlot((uint8_t)i))++valid;
    if(presetRam[i-1].validMorph)++morph;
    if(i==1 || (i%4)==0 || i==128)drawBootProgress(i,128,"BUILDING PRESET CACHE");
    if((i%32)==0)Serial.printf("PRESET RAM: %u/128 in %lu ms\n",(unsigned)i,(unsigned long)(millis()-start));
    yield();
  }
  presetRamReady=true;
  drawBootProgress(128,128,"SAVING PRESET CACHE");
  Serial.printf("PRESET CACHE: %s\n",presetCacheWrite()?"SAVED":"SAVE FAILED (RAM STILL READY)");
  Serial.printf("PRESET RAM: READY valid=%u morph=%u bytes=%u build=%lu ms\n",valid,morph,
                (unsigned)sizeof(presetRam),(unsigned long)(millis()-start));
}
static void presetRamMorphApply(uint8_t slot);

static void savePreset(uint8_t slot){
  // FIX13A: isolated wall-clock timings, no changes to SD write semantics.
  uint32_t stepStart=0;
  auto profileStep=[](const char* label,uint32_t t){
    Serial.printf("SD PROFILE: %-22s %lu ms\n",label,(unsigned long)(millis()-t));
  };
  if(slot<1 || slot>128)return;
  if(!sdReady){showNotice("SD NOT READY");return;}
  const uint32_t saveStart=millis();
  // FIX13B: Fast no-op only when the active slot has a validated RAM snapshot,
  // the main and advanced parameter bytes match, and no morph CFG is indexed.
  // Morph slots intentionally retain the original complete save path.
  // The persistent cache is loaded with checksum verification at boot.
  if(presetRamReady && activePreset==slot &&
     presetRam[slot-1].validMain && !morphConfigCached(slot) && !morphConfigDirty &&
     memcmp(presetRam[slot-1].main,value,PARAM_COUNT)==0 &&
     memcmp(presetRam[slot-1].advanced,adv,ADV_COUNT)==0){
    Serial.printf("SD SAVE: SMART UNCHANGED %03u (NO SD WRITES)\n",(unsigned)slot);
    sdProfile("TOTAL SAVE",saveStart);
    showNotice("PRESET UNCHANGED");
    return;
  }
  stepStart=millis(); sdProgress("SAVING PRESET"); profileStep("DISPLAY START",stepStart);
  FluxPreset p={};p.magic=0x52544658UL;p.version=1;p.count=PARAM_COUNT;
  memcpy(p.params,value,PARAM_COUNT);p.checksum=checksumPreset(p);
  char path[48],tmp[48];presetPath(path,sizeof(path),slot);snprintf(tmp,sizeof(tmp),slot<=8?"/RTAL_FLANGER/FLUX%02u.TMP":"/RTAL_FLANGER/FLUX%03u.TMP",(unsigned)slot);
  stepStart=millis(); SD.remove(tmp); profileStep("MAIN REMOVE TEMP",stepStart);
  stepStart=millis(); File f=SD.open(tmp,FILE_WRITE); profileStep("MAIN OPEN TEMP",stepStart);
  if(!f){showNotice("SAVE ERROR");return;}
  stepStart=millis(); size_t n=f.write((const uint8_t*)&p,sizeof(p)); profileStep("MAIN WRITE 36B",stepStart);
  stepStart=millis(); f.flush(); profileStep("MAIN FLUSH",stepStart);
  stepStart=millis(); f.close(); profileStep("MAIN CLOSE",stepStart);
  if(n!=sizeof(p)){SD.remove(tmp);showNotice("SAVE ERROR");return;}
  sdProfile("MAIN PRESET WRITE",saveStart);
  stepStart=millis(); sdProgress("UPDATING PRESET"); profileStep("DISPLAY PRESET",stepStart);
  stepStart=millis(); SD.remove(path); profileStep("MAIN REMOVE OLD",stepStart);
  stepStart=millis(); bool renamed=SD.rename(tmp,path); profileStep("MAIN RENAME",stepStart);
  if(!renamed){showNotice("RENAME ERROR");return;}
  sdProfile("MAIN PRESET RENAME",saveStart);
  stepStart=millis(); sdProgress("SAVING ADVANCED"); profileStep("DISPLAY ADVANCED",stepStart);
  stepStart=millis(); saveAdvanced(slot); profileStep("ADVANCED TOTAL",stepStart);
  sdProfile("ADVANCED DATA",saveStart);
  // FIX12A: Ordinary preset overwrite must not CREATE morph metadata.
  // Keep existing morph configurations editable/savable, but leave slots
  // without a morph CFG untouched. Use SD.exists as source of truth here,
  // rather than trusting a potentially stale index bitmap.
  stepStart=millis(); bool hasMorph=morphConfigExists(slot); profileStep("MORPH EXISTS",stepStart);
  const bool saveMorph=morphConfigDirty;
  // FIX13E: Existing unchanged morph configuration is already persistent.
  // Only explicit morph edits create or rewrite the CFG.
  Serial.printf("MORPH SAVE DECISION: slot=%03u existing=%u dirty=%u save=%u\n",(unsigned)slot,(unsigned)hasMorph,(unsigned)morphConfigDirty,(unsigned)saveMorph);
  if(saveMorph){
    sdProgress("SAVING MORPH");
    stepStart=millis(); bool morphSaved=morphConfigSave(slot); profileStep("MORPH SAVE",stepStart);
    if(!morphSaved){showNotice("MORPH SAVE ERROR");return;}
    morphConfigDirty=false;
    hasMorph=true; // newly created CFG must also enter the RAM cache and M marker
    sdProfile("MORPH DATA",saveStart);
  }else{
    Serial.printf("SD SAVE: MORPH SKIPPED %03u (%s)\n",(unsigned)slot,hasMorph?"EXISTING UNCHANGED CFG":"NO EXISTING CFG");
    // Reconcile stale morph index entries, if present.
    if(!hasMorph && morphConfigFlagsReady && morphConfigFlags[slot-1]){
      // Only clear a genuinely stale index entry. An unchanged existing
      // Morph CFG must keep its marker and cache flag.
      morphConfigFlags[slot-1]=false;
      if(!morphIndexWrite())Serial.println("MORPH INDEX: RECONCILE FAILED");
    }
  }
  if(presetRamReady){
    stepStart=millis(); sdProgress("UPDATING CACHE"); profileStep("DISPLAY CACHE",stepStart);
    // FIX13G: update the RAM snapshot directly from the bytes successfully
    // written to SD. Avoid reopening three files for existing Morph slots.
    // Only trust an unchanged Morph snapshot if it was validated at boot.
    stepStart=millis();
    bool ramRead=true;
    CachedPreset &c=presetRam[slot-1];
    const bool hadValidMorph=c.validMorph;
    uint8_t newMorph[14]={};
    bool haveMorphBytes=false;
    if(hasMorph){
      if(saveMorph){
        const uint8_t fields[13]={'M','F',1,morphSlotA,morphSlotB,morphPosition,morphTime,
          morphMode,morphCurve,(uint8_t)morphPingPong,(uint8_t)morphSync,
          morphDivision,(uint8_t)morphReverse};
        memcpy(newMorph,fields,sizeof(fields));
        uint8_t check=0;for(uint8_t i=0;i<13;++i)check^=newMorph[i];
        newMorph[13]=check;
        haveMorphBytes=true;
      }else if(hadValidMorph){
        memcpy(newMorph,c.morph,sizeof(newMorph));
        haveMorphBytes=true;
      }
    }
    if(!hasMorph || haveMorphBytes){
      memcpy(c.main,p.params,PARAM_COUNT);
      memcpy(c.advanced,adv,ADV_COUNT);
      c.validMain=true;
      c.validMorph=hasMorph;
      if(hasMorph)memcpy(c.morph,newMorph,sizeof(newMorph));
      else memset(c.morph,0,sizeof(c.morph));
      profileStep("RAM DIRECT UPDATE",stepStart);
    }else{
      // An indexed Morph CFG can be missing/invalid in the RAM snapshot.
      // Preserve the original SD validation path in this exceptional case.
      ramRead=presetRamReadSlot(slot);
      profileStep("RAM FALLBACK SD READ",stepStart);
    }
    if(ramRead){
      Serial.printf("PRESET RAM: UPDATED %03u\n",(unsigned)slot);
      stepStart=millis(); bool cacheSaved=presetCacheUpdateSlot(slot);
      if(!cacheSaved){
        Serial.println("SD CACHE: FALLBACK FULL REWRITE");
        cacheSaved=presetCacheWrite();
        if(!cacheSaved){
          // Never leave a potentially stale cache trusted on the next boot.
          SD.remove(PRESET_CACHE_PATH);
          Serial.println("SD CACHE: INVALIDATED; REBUILD ON NEXT BOOT");
        }
      }
      profileStep("CACHE WRITE TOTAL",stepStart);
      Serial.printf("PRESET CACHE: %s\n",cacheSaved?"SAVED":"SAVE FAILED (REBUILD NEXT BOOT)");
    }else{
      SD.remove(PRESET_CACHE_PATH);Serial.println("PRESET CACHE: INVALIDATED AFTER SAVE");
    }
  }
  sdProfile("TOTAL SAVE",saveStart);
  Serial.println("SD PROFILE: END");
  activePreset=slot;char msg[26];snprintf(msg,sizeof(msg),"PRESET %u SAVED",slot);showNotice(msg);
}
static void morphDeactivate();
static void loadPreset(uint8_t slot){
  morphConfigDirty=false;
  const uint32_t profileStart=millis();
  uint32_t profileMain=0,profileApply=0,profileAdv=0,profileMorph=0;
  morphDeactivate();
  if(slot<1 || slot>128)return;
  if(!sdReady){showNotice("SD NOT READY");return;}
  if(presetRamReady){
    const CachedPreset &c=presetRam[slot-1];
    if(!c.validMain){showNotice("PRESET EMPTY");return;}
    memcpy(value,c.main,PARAM_COUNT);
    for(uint8_t i=0;i<PARAM_COUNT;++i)applyValue(i);
    memcpy(adv,c.advanced,ADV_COUNT);applyAllAdvanced();
    presetRamMorphApply(slot);
    activePreset=slot;presetBank=(uint8_t)((slot-1)/8);
    Serial.printf("PRESET RAM LOAD %03u: %lu us\n",(unsigned)slot,(unsigned long)(millis()-profileStart)*1000UL);
    char msg[26];snprintf(msg,sizeof(msg),"PRESET %u LOADED",slot);showNotice(msg);
    return;
  }
  char path[48];presetPath(path,sizeof(path),slot);File f=SD.open(path,FILE_READ);
  if(!f){showNotice("PRESET EMPTY");return;}
  FluxPreset p={};bool ok=(f.size()==sizeof(p) && f.read((uint8_t*)&p,sizeof(p))==sizeof(p));f.close();
  if(!ok || p.magic!=0x52544658UL || p.version!=1 || p.count!=PARAM_COUNT || p.checksum!=checksumPreset(p)) {showNotice("INVALID PRESET");return;}
  for(uint8_t i=0;i<PARAM_COUNT;++i) if(p.params[i]<paramMin(i)||p.params[i]>paramMax(i)){showNotice("INVALID VALUE");return;}
  profileMain=millis()-profileStart;
  const uint32_t applyStart=millis();
  memcpy(value,p.params,PARAM_COUNT);
  for(uint8_t i=0;i<PARAM_COUNT;++i) applyValue(i);
  profileApply=millis()-applyStart;
  const uint32_t advStart=millis();loadAdvanced(slot);profileAdv=millis()-advStart;
  const uint32_t morphStart=millis();morphConfigLoad(slot);profileMorph=millis()-morphStart;
  Serial.printf("PRESET PROFILE %03u: MAIN=%lu APPLY=%lu ADV=%lu MORPH=%lu TOTAL=%lu ms\n",
    (unsigned)slot,(unsigned long)profileMain,(unsigned long)profileApply,
    (unsigned long)profileAdv,(unsigned long)profileMorph,(unsigned long)(millis()-profileStart));
  activePreset=slot;presetBank=(uint8_t)((slot-1)/8);char msg[26];snprintf(msg,sizeof(msg),"PRESET %u LOADED",slot);showNotice(msg);
}

// F009E FIX1: morphing reads two SD presets into RAM without modifying SD files.
static MorphSnapshot morphA, morphB;
static bool morphReadyA=false, morphReadyB=false, morphActive=false;
// Arduino auto-prototype generation cannot safely forward-declare MorphSnapshot.
// Explicit prototype after the struct definition keeps the type visible.
static bool morphRead(uint8_t slot, MorphSnapshot &dest);
static uint32_t morphLastMs=0;
static void morphDeactivate(){morphActive=false;morphMode=0;}
static float morphAutoPos=0.0f;
static uint8_t morphFavA[16]={}, morphFavB[16]={};
static bool morphFavValid[16]={};
static uint8_t morphGate[4]={}; // edge state for START, SAVE, LOAD, STOP
static uint32_t morphClockStartTick=0;
static const uint8_t MORPH_CC[15]={106,109,110,111,112,115,118,119,75,76,86,89,90,77,78};
static const float MORPH_BARS[8]={0.0625f,0.125f,0.25f,0.5f,1.0f,2.0f,4.0f,8.0f};
// RC3: per-slot morph metadata. Separate from legacy FLUX BIN/ADV format.
// Format: MF, version 1, 10 config bytes, 1 XOR checksum (13 bytes total).
// No recursive load: morphRead() reads sound snapshots only.
static void morphConfigPath(char* out,size_t n,uint8_t slot){
  snprintf(out,n,"/RTAL_FLANGER/MORPH%03u.CFG",(unsigned)slot);
}
static bool morphConfigExists(uint8_t slot){
  if(!sdReady || slot<1 || slot>128)return false;
  char path[48];morphConfigPath(path,sizeof(path),slot);
  return SD.exists(path);
}
static bool morphConfigSave(uint8_t slot){
  if(!sdReady || slot<1 || slot>128)return false;
  const uint32_t morphStart=millis();
  uint32_t phase=millis();
  const uint8_t b[13]={'M','F',1,morphSlotA,morphSlotB,morphPosition,morphTime,
    morphMode,morphCurve,(uint8_t)morphPingPong,(uint8_t)morphSync,
    morphDivision,(uint8_t)morphReverse};
  char path[48],tmp[48];morphConfigPath(path,sizeof(path),slot);
  snprintf(tmp,sizeof(tmp),"/RTAL_FLANGER/MORPH%03u.TMP",(unsigned)slot);
  phase=millis();bool removedTemp=SD.remove(tmp);
  Serial.printf("MORPH PROFILE: REMOVE TEMP       %lu ms (found=%u)\n",(unsigned long)(millis()-phase),(unsigned)removedTemp);
  phase=millis();File f=SD.open(tmp,FILE_WRITE);
  Serial.printf("MORPH PROFILE: OPEN TEMP         %lu ms (ok=%u)\n",(unsigned long)(millis()-phase),(unsigned)(bool)f);
  if(!f)return false;
  uint8_t chk=0;for(uint8_t i=0;i<sizeof(b);++i)chk^=b[i];
  phase=millis();bool ok=(f.write(b,sizeof(b))==sizeof(b));
  ok=(f.write(&chk,1)==1)&&ok;
  Serial.printf("MORPH PROFILE: WRITE 14B         %lu ms (ok=%u)\n",(unsigned long)(millis()-phase),(unsigned)ok);
  phase=millis();f.flush();
  Serial.printf("MORPH PROFILE: FLUSH             %lu ms\n",(unsigned long)(millis()-phase));
  phase=millis();f.close();
  Serial.printf("MORPH PROFILE: CLOSE             %lu ms\n",(unsigned long)(millis()-phase));
  if(!ok){SD.remove(tmp);return false;}
  phase=millis();bool removedOld=SD.remove(path);
  Serial.printf("MORPH PROFILE: REMOVE OLD        %lu ms (found=%u)\n",(unsigned long)(millis()-phase),(unsigned)removedOld);
  phase=millis();bool renamed=SD.rename(tmp,path);
  Serial.printf("MORPH PROFILE: RENAME            %lu ms (ok=%u)\n",(unsigned long)(millis()-phase),(unsigned)renamed);
  if(!renamed){SD.remove(tmp);return false;}
  if(morphConfigFlagsReady){
    const bool indexChanged=!morphConfigFlags[slot-1];
    morphConfigFlags[slot-1]=true;
    if(indexChanged){
      phase=millis();bool indexOk=morphIndexWrite();
      Serial.printf("MORPH PROFILE: INDEX WRITE       %lu ms (ok=%u)\n",(unsigned long)(millis()-phase),(unsigned)indexOk);
      if(!indexOk){
        // Preserve the on-disk CFG, but do not report a successful index update.
        // Restore the RAM flag so the next save retries the index write.
        morphConfigFlags[slot-1]=false;
        Serial.printf("MORPH INDEX: RETRY REQUIRED slot=%03u\n",(unsigned)slot);
        return false;
      }
    }else{
      // Existing slot: its index bit is already set, so no SD index write.
      Serial.printf("MORPH PROFILE: INDEX UNCHANGED slot=%03u 0 ms (SKIPPED)\n",(unsigned)slot);
    }
  }
  Serial.printf("MORPH PROFILE: TOTAL             %lu ms\n",(unsigned long)(millis()-morphStart));
  return true;
}
static void morphConfigLoad(uint8_t slot){
  morphConfigDirty=false;
  morphRunning=false;morphActive=false;morphReadyA=false;morphReadyB=false;
  morphSlotA=1;morphSlotB=2;morphPosition=0;morphAutoPos=0;
  morphTime=35;morphMode=0;morphCurve=0;morphPingPong=false;
  morphSync=false;morphDivision=4;morphReverse=false;
  if(!morphConfigExists(slot))return; // old preset: neutral defaults
  char path[48];morphConfigPath(path,sizeof(path),slot);
  File f=SD.open(path,FILE_READ);if(!f)return;
  uint8_t b[14]={};bool ok=(f.size()==sizeof(b) && f.read(b,sizeof(b))==sizeof(b));f.close();
  uint8_t chk=0;for(uint8_t i=0;i<13;++i)chk^=b[i];
  if(!ok||b[0]!='M'||b[1]!='F'||b[2]!=1||chk!=b[13]||
     b[3]<1||b[3]>128||b[4]<1||b[4]>128||b[5]>100||b[6]>100||
     b[7]>1||b[8]>1||b[9]>1||b[10]>1||b[11]>7||b[12]>1){
    Serial.printf("MORPH CFG invalid slot %u\n",(unsigned)slot);return;
  }
  morphSlotA=b[3];morphSlotB=b[4];morphPosition=b[5];morphAutoPos=b[5];
  morphTime=b[6];morphMode=b[7];morphCurve=b[8];morphPingPong=b[9];
  morphSync=b[10];morphDivision=b[11];morphReverse=b[12];
  // Preserve configured AUTO mode, but never automatically start on preset load.
  morphRunning=false;
}
static void presetRamMorphApply(uint8_t slot){
  morphConfigDirty=false;
  morphRunning=false;morphActive=false;morphReadyA=false;morphReadyB=false;
  morphSlotA=1;morphSlotB=2;morphPosition=0;morphAutoPos=0;
  morphTime=35;morphMode=0;morphCurve=0;morphPingPong=false;
  morphSync=false;morphDivision=4;morphReverse=false;
  const CachedPreset &c=presetRam[slot-1];
  if(!c.validMorph)return;
  const uint8_t *b=c.morph;
  morphSlotA=b[3];morphSlotB=b[4];morphPosition=b[5];morphAutoPos=b[5];
  morphTime=b[6];morphMode=b[7];morphCurve=b[8];morphPingPong=b[9];
  morphSync=b[10];morphDivision=b[11];morphReverse=b[12];
}
static void morphSaveFavorites(){
  if(!sdReady)return;
  // FILE_WRITE appends on ESP32: discard any stale temporary file first.
  SD.remove("/RTAL_FLANGER/MORPHFAV.TMP");
  File f=SD.open("/RTAL_FLANGER/MORPHFAV.TMP",FILE_WRITE);if(!f)return;
  const uint8_t hdr[3]={'M','F',1};f.write(hdr,3);
  for(uint8_t i=0;i<16;i++){uint8_t b[3]={uint8_t(morphFavValid[i]),morphFavA[i],morphFavB[i]};f.write(b,3);}
  f.flush();const bool complete=(f.size()==51);f.close();
  if(!complete){SD.remove("/RTAL_FLANGER/MORPHFAV.TMP");showNotice("FAVORITE WRITE ERROR");return;}
  SD.remove("/RTAL_FLANGER/MORPHFAV.CFG");
  if(!SD.rename("/RTAL_FLANGER/MORPHFAV.TMP","/RTAL_FLANGER/MORPHFAV.CFG"))showNotice("FAVORITE RENAME ERROR");
}
static void morphLoadFavorites(){
  if(!sdReady)return;File f=SD.open("/RTAL_FLANGER/MORPHFAV.CFG",FILE_READ);
  if(!f)return;uint8_t b[51]={};bool ok=f.size()==sizeof(b)&&f.read(b,sizeof(b))==sizeof(b);f.close();
  if(!ok||b[0]!='M'||b[1]!='F'||b[2]!=1)return;
  for(uint8_t i=0;i<16;i++){uint8_t a=b[3+3*i+1],c=b[3+3*i+2];
    morphFavValid[i]=b[3+3*i]&&a>=1&&c>=1&&a<=128&&c<=128;
    morphFavA[i]=a;morphFavB[i]=c;}
}
static void morphApply();
static void morphLoadPair();
static void morphBank1CC(uint8_t cc,uint8_t v){
  uint8_t idx=255;for(uint8_t i=0;i<15;i++)if(cc==MORPH_CC[i]){idx=i;break;}
  if(idx==255)return;
  if(idx<=8 || idx==11 || idx==14)morphConfigDirty=true;
  switch(idx){
    case 0:morphPosition=(uint16_t(v)*100U+63U)/127U;morphAutoPos=morphPosition;morphMode=0;morphRunning=false;morphApply();break;
    case 1:morphSlotA=v+1;morphActive=false;break;
    case 2:morphSlotB=v+1;morphActive=false;break;
    case 3:morphTime=(uint16_t(v)*100U+63U)/127U;break;
    case 4:morphMode=(v>=64);morphLastMs=millis();morphAutoPos=morphPosition;morphRunning=(morphMode!=0);morphClockStartTick=midiClock.ticks();break;
    case 5:morphCurve=(v>=64);morphApply();break;
    case 6:morphPingPong=(v>=64);break;
    case 7:morphSync=(v>=64);morphClockStartTick=midiClock.ticks();break;
    case 8:morphDivision=(uint16_t(v)*7U+63U)/127U;break;
    case 9:if(v>=64&&!morphGate[0]){morphMode=1;morphRunning=true;morphLastMs=millis();morphAutoPos=morphPosition;morphClockStartTick=midiClock.ticks();}morphGate[0]=(v>=64);break;
    case 10:if(v>=64&&!morphGate[3]){morphRunning=false;morphMode=0;}morphGate[3]=(v>=64);break;
    case 11:if(morphReverse!=(v>=64)){morphReverse=(v>=64);morphClockStartTick=midiClock.ticks();}break;
    case 12:morphFavorite=(uint16_t(v)*15U+63U)/127U;break;
    case 13:if(v>=64&&!morphGate[1]){morphFavA[morphFavorite]=morphSlotA;morphFavB[morphFavorite]=morphSlotB;morphFavValid[morphFavorite]=true;morphSaveFavorites();showNotice("MORPH FAVORITE SAVED");}morphGate[1]=(v>=64);break;
    case 14:if(v>=64&&!morphGate[2]){if(morphFavValid[morphFavorite]){morphSlotA=morphFavA[morphFavorite];morphSlotB=morphFavB[morphFavorite];morphLoadPair();}else showNotice("FAVORITE EMPTY");}morphGate[2]=(v>=64);break;
  }
}

static bool morphRead(uint8_t slot,MorphSnapshot &dest){
  if(!sdReady||slot<1||slot>128)return false;
  if(presetRamReady){
    const CachedPreset &c=presetRam[slot-1];
    if(!c.validMain)return false;
    memcpy(dest.legacy,c.main,PARAM_COUNT);
    memcpy(dest.extra,c.advanced,ADV_COUNT);
    return true;
  }
  char path[48];presetPath(path,sizeof(path),slot);
  File f=SD.open(path,FILE_READ);if(!f)return false;
  FluxPreset p={};bool ok=f.size()==sizeof(p)&&f.read((uint8_t*)&p,sizeof(p))==sizeof(p);f.close();
  if(!ok||p.magic!=0x52544658UL||p.version!=1||p.count!=PARAM_COUNT||p.checksum!=checksumPreset(p))return false;
  for(uint8_t i=0;i<PARAM_COUNT;++i)if(p.params[i]<paramMin(i)||p.params[i]>paramMax(i))return false;
  memcpy(dest.legacy,p.params,PARAM_COUNT);
  const uint8_t defaults[ADV_COUNT]={35,0,0,0,0,0,0,0,0,0,0,0,20,50,0};
  memcpy(dest.extra,defaults,ADV_COUNT);
  advancedPath(path,sizeof(path),slot);f=SD.open(path,FILE_READ);
  if(f){const size_t length=f.size();uint8_t blob[4+ADV_COUNT]={};
    ok=(length==10||length==14||length==sizeof(blob))&&f.read(blob,length)==length;f.close();
    if(ok&&blob[0]=='A'&&blob[1]=='D'&&
       ((blob[2]==1&&blob[3]==6&&length==10)||(blob[2]==2&&blob[3]==10&&length==14)||
        (blob[2]==3&&blob[3]==ADV_COUNT&&length==sizeof(blob)))){
      bool valid=true;for(uint8_t i=0;i<blob[3];++i)if(blob[4+i]>advMax(i))valid=false;
      if(valid)memcpy(dest.extra,blob+4,blob[3]);
    }
  }
  return true;
}
static void morphApply(){
  if(!morphActive||!morphReadyA||!morphReadyB)return;
  float t=morphPosition*0.01f;
  if(morphCurve)t=t*t*(3.0f-2.0f*t);
  for(uint8_t i=0;i<PARAM_COUNT;++i){
    // Never interpolate mode selectors, sync switches or waveform IDs.
    const bool discrete=(i==LFO_WAVE||i==SYNC_ENABLE||i==SYNC_DIV||i==RESET_START||
      i==MODE_PARAM||i==BYPASS_PARAM||i==STEREO_MODE_PARAM||i==INPUT_PARAM);
    const uint8_t a=morphA.legacy[i],b=morphB.legacy[i];
    uint8_t v=discrete?(t<0.5f?a:b):(uint8_t)(a+(b-a)*t+0.5f);
    if(v!=value[i]){value[i]=v;applyValue(i);}
  }
  for(uint8_t i=0;i<ADV_COUNT;++i){
    const bool discrete=(i==A_WAVE||i==A_TARGET||i==A_CHARACTER||i==A_MATRIX||i==A_ENV_TARGET);
    const uint8_t a=morphA.extra[i],b=morphB.extra[i];
    uint8_t v=discrete?(t<0.5f?a:b):(uint8_t)(a+(b-a)*t+0.5f);
    if(v!=adv[i]){adv[i]=v;applyAdvanced(i);}
  }
}
static void morphLoadPair(){
  morphReadyA=morphRead(morphSlotA,morphA);morphReadyB=morphRead(morphSlotB,morphB);
  morphActive=morphReadyA&&morphReadyB;
  if(!morphActive)showNotice("MORPH PRESET ERROR");
  else {morphPosition=0;morphAutoPos=0.0f;morphLastMs=millis();morphApply();showNotice("MORPH PAIR READY");}
}
static void morphUpdate(){
  if(!morphActive||morphMode==0||!morphRunning)return;
  const uint32_t now=millis();uint32_t delta=now-morphLastMs;morphLastMs=now;
  if(delta>200U)delta=200U;
  const float durationMs=500.0f+((float)morphTime/100.0f)*29500.0f;
  float movement=100.0f*(float)delta/durationMs;
  if(morphSync && midiClock.playing() && midiClock.active(micros())){
    const uint32_t ticks=midiClock.ticks()-morphClockStartTick;
    const float targetTicks=96.0f*MORPH_BARS[morphDivision<8?morphDivision:7];
    const float target=(float)ticks*100.0f/targetTicks;
    movement=target-morphAutoPos;
    if(morphReverse)movement=-movement;
  }else if(morphReverse)movement=-movement;
  morphAutoPos+=movement;
  if(morphAutoPos>=100.0f){morphAutoPos=100.0f;if(morphPingPong){morphReverse=true;morphClockStartTick=midiClock.ticks();}else{morphMode=0;morphRunning=false;}}
  if(morphAutoPos<=0.0f){morphAutoPos=0.0f;if(morphPingPong){morphReverse=false;morphClockStartTick=midiClock.ticks();}else{morphMode=0;morphRunning=false;}}
  const uint8_t next=(uint8_t)(morphAutoPos+0.5f);
  if(next!=morphPosition){morphPosition=next;morphApply();}
}
// All parameters, including advanced controls, use CC32=0.
// CC32=1 is reserved for future use.
// Fixed MIDI channel 1; PC0..7 loads the eight existing SD presets.
static uint8_t midiChannel = 1; // 0=OMNI, 1..16
static bool midiFeedback = false;
enum FluxUiState { UI_HOME, UI_MENU, UI_PAGE, UI_EDIT, UI_PRESET_BROWSER, UI_PRESET_ACTION, UI_PRESET_RENAME };
static FluxUiState uiState=UI_HOME;
// RC2: names are stored separately; existing FLUX*.BIN/ADV files are untouched.
static uint8_t browserSlot=1, browserAction=0, renamePos=0;
static char renameText[20]={0};
// Cache preset names in RAM: no SD access during browser redraws.
static char browserNames[128][20];
static bool browserNamesLoaded=false;
static void presetBrowserCacheInvalidate(){browserNamesLoaded=false;}
static const char PRESET_ALPHABET[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_./";
static void presetNamePath(char *out,size_t n,uint8_t slot){snprintf(out,n,"/RTAL_FLANGER/NAME%03u.TXT",(unsigned)slot);}
static void presetName(uint8_t slot,char *out,size_t n){
  if(!n)return;
  snprintf(out,n,"PRESET %03u",(unsigned)slot);
  if(!sdReady||slot<1||slot>128)return;
  char path[48];presetNamePath(path,sizeof(path),slot);
  File f=SD.open(path,FILE_READ);
  // Compatible with the Phaser-style two-digit name files for slots 1..99.
  if(!f && slot<=99){snprintf(path,sizeof(path),"/RTAL_FLANGER/NAME%02u.TXT",(unsigned)slot);f=SD.open(path,FILE_READ);}
  if(!f)return;
  char temp[20]={0};size_t k=0;
  while(f.available()&&k<19){int c=f.read();if(c=='\n'||c=='\r')break;if(c>=32&&c<=126)temp[k++]=(char)c;}
  f.close();while(k&&temp[k-1]==' ')temp[--k]=0;
  if(k)snprintf(out,n,"%s",temp);
}
// FIX7: compact binary name index, 128 x 20-byte records, with checksum.
// The legacy NAME###.TXT files remain the authoritative, compatible source.
static const char* NAME_INDEX_PATH="/RTAL_FLANGER/PRESET_NAMES.DAT";
static const char* NAME_INDEX_TEMP="/RTAL_FLANGER/PRESET_NAMES.TMP";
static uint32_t namesChecksum(){
  uint32_t hash=2166136261UL;
  const uint8_t* p=(const uint8_t*)browserNames;
  for(size_t i=0;i<sizeof(browserNames);++i){hash^=p[i];hash*=16777619UL;}
  return hash;
}
static bool namesIndexRead(){
  if(!sdReady)return false;
  File f=SD.open(NAME_INDEX_PATH,FILE_READ);
  if(!f)return false;
  const size_t expected=8+sizeof(browserNames);
  if(f.size()!=expected){f.close();return false;}
  uint8_t h[8];
  bool ok=f.read(h,8)==8 && h[0]=='R' && h[1]=='F' && h[2]=='N' && h[3]==1;
  uint32_t stored=(uint32_t)h[4]|((uint32_t)h[5]<<8)|((uint32_t)h[6]<<16)|((uint32_t)h[7]<<24);
  if(ok)ok=f.read((uint8_t*)browserNames,sizeof(browserNames))==sizeof(browserNames);
  f.close();
  if(!ok || stored!=namesChecksum())return false;
  for(uint16_t i=0;i<128;++i)if(!memchr(browserNames[i],0,20))return false;
  return true;
}
static bool namesIndexWrite(){
  if(!sdReady)return false;
  SD.remove(NAME_INDEX_TEMP);
  File f=SD.open(NAME_INDEX_TEMP,FILE_WRITE);
  if(!f)return false;
  uint32_t check=namesChecksum();
  uint8_t h[8]={'R','F','N',1,(uint8_t)check,(uint8_t)(check>>8),(uint8_t)(check>>16),(uint8_t)(check>>24)};
  bool ok=f.write(h,8)==8 && f.write((const uint8_t*)browserNames,sizeof(browserNames))==sizeof(browserNames);
  f.flush();f.close();
  if(!ok){SD.remove(NAME_INDEX_TEMP);return false;}
  // Keep a valid old index if the temporary file is incomplete.
  SD.remove(NAME_INDEX_PATH);
  if(!SD.rename(NAME_INDEX_TEMP,NAME_INDEX_PATH))return false;
  return true;
}
// FIX9: Always reconstruct the splash before drawing status; never overlay bitmap pixels.
// The lower 27 pixel rows are reserved exclusively for progress information.
static void drawBootProgress(uint16_t loaded,uint16_t total,const char* phase){
  if(!total)total=1;
  if(loaded>total)loaded=total;
  u8g2.clearBuffer();
  u8g2.drawXBMP(0,0,128,64,RTAL_FLANGER_SPLASH);
  u8g2.setDrawColor(0);
  u8g2.drawBox(0,36,128,28);
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2,43,phase);
  char line[32];
  snprintf(line,sizeof(line),"PRESETS %u/%u   %u%%",(unsigned)loaded,(unsigned)total,
           (unsigned)((uint32_t)loaded*100U/total));
  u8g2.drawStr(2,52,line);
  u8g2.drawFrame(1,56,126,8);
  const uint8_t width=(uint8_t)((uint32_t)loaded*122U/total);
  if(width)u8g2.drawBox(3,58,width,4);
  u8g2.sendBuffer();
}
static void drawNamesBootProgress(uint16_t loaded,uint16_t total,const char* phase){
  drawBootProgress(loaded,total,phase);
}
static void presetBrowserCacheLoad(){
  if(browserNamesLoaded)return;
  if(namesIndexRead()){
    browserNamesLoaded=true;
    drawNamesBootProgress(128,128,"NAME INDEX READY");
    Serial.println("NAME CACHE: index loaded");
    return;
  }
  Serial.println("NAME CACHE: importing legacy name files");
  for(uint16_t i=1;i<=128;++i){
    presetName((uint8_t)i,browserNames[i-1],sizeof(browserNames[0]));
    // Display actual progress; keep OLED I/O modest compared to SD I/O.
    if(i==1 || (i%4)==0)drawNamesBootProgress(i,128,"IMPORTING NAMES");
    yield();
  }
  browserNamesLoaded=true;
  if(sdReady && !namesIndexWrite())Serial.println("NAME CACHE: index write failed");
}
static const char* presetBrowserCachedName(uint8_t slot){
  if(!browserNamesLoaded)presetBrowserCacheLoad();
  return browserNames[slot-1];
}
static void presetNameSave(uint8_t slot,const char *name){
  if(!sdReady){showNotice("SD NOT READY");return;}
  char path[48],tmp[48];presetNamePath(path,sizeof(path),slot);
  snprintf(tmp,sizeof(tmp),"/RTAL_FLANGER/NAM%03u.TMP",(unsigned)slot);
  SD.remove(tmp);File f=SD.open(tmp,FILE_WRITE);
  if(!f){showNotice("NAME SAVE ERROR");return;}
  size_t len=strlen(name);while(len&&name[len-1]==' ')--len;
  size_t written=f.write((const uint8_t*)name,len);f.flush();f.close();
  if(written!=len){SD.remove(tmp);showNotice("NAME SAVE ERROR");return;}
  // Keep the previous name if temporary file was not fully written.
  SD.remove(path);
  if(!SD.rename(tmp,path)){showNotice("NAME RENAME ERROR");return;}
  // Update only the renamed slot: never invalidate/reload all 128 names.
  if(slot>=1 && slot<=128 && browserNamesLoaded){
    snprintf(browserNames[slot-1],sizeof(browserNames[0]),"%.*s",(int)len,name);
    if(!namesIndexWrite())Serial.println("NAME CACHE: index update failed");
  }
  showNotice("NAME SAVED");
}
static void presetBeginRename(){
  presetName(browserSlot,renameText,sizeof(renameText));
  // Replace default placeholder with an editable blank name for empty slots.
  char fallback[20];snprintf(fallback,sizeof(fallback),"PRESET %03u",(unsigned)browserSlot);
  if(strcmp(renameText,fallback)==0)renameText[0]=0;
  renamePos=0;uiState=UI_PRESET_RENAME;
}
static void drawPresetBrowser(){
  // Phaser-style five-row layout. Names are cached in RAM to avoid SD reads on scroll.
  u8g2.clearBuffer();u8g2.setFont(u8g2_font_5x7_tf);
  char line[48],name[20];
  if(uiState==UI_PRESET_BROWSER || uiState==UI_PRESET_ACTION){
    u8g2.drawStr(0,7,uiState==UI_PRESET_BROWSER?"PRESET BROWSER 1-128":"PRESET ACTION");
    u8g2.drawHLine(0,12,128);
    if(uiState==UI_PRESET_BROWSER){
      const int start=max(1,min(124,(int)browserSlot-2));
      for(int j=0;j<5;++j){
        const uint8_t slot=(uint8_t)(start+j);
        snprintf(line,sizeof(line),"%c%03u%c %-18.18s",slot==browserSlot?'>':' ',(unsigned)slot,morphConfigCached(slot)?'M':' ',presetBrowserCachedName(slot));
        u8g2.drawStr(0,22+j*8,line);
      }
      u8g2.drawStr(0,63,"PRESS: ACTION  HOLD: HOME");
    }else{
      snprintf(line,sizeof(line),"%03u%c %-19.19s",(unsigned)browserSlot,morphConfigCached(browserSlot)?'M':' ',presetBrowserCachedName(browserSlot));
      u8g2.drawStr(0,22,line);
      static const char* const actions[]={"LOAD","SAVE (OVERWRITE)","RENAME"};
      for(uint8_t i=0;i<3;++i){
        snprintf(line,sizeof(line),"%c %s",i==browserAction?'>':' ',actions[i]);
        u8g2.drawStr(0,34+i*8,line);
      }
      u8g2.drawHLine(0,53,128);
      u8g2.drawStr(0,63,"PRESS: EXECUTE");
    }
  }else{
    snprintf(line,sizeof(line),"RENAME PRESET %03u",(unsigned)browserSlot);
    u8g2.drawStr(0,7,line);u8g2.drawHLine(0,12,128);
    char display[20];memcpy(display,renameText,sizeof(display));display[19]=0;
    for(uint8_t i=0;i<19;++i)if(!display[i])display[i]=' ';
    u8g2.drawStr(0,27,display);
    // The 5x7 font uses a 5-pixel character advance, not 6 pixels.
    // Measure the actual rendered prefix to align the edit cursor.
    char prefix[20]; memcpy(prefix,display,renamePos); prefix[renamePos]=0;
    const uint8_t cursorX=(uint8_t)u8g2.getStrWidth(prefix);
    u8g2.drawHLine(cursorX,30,4);
    u8g2.drawStr(0,43,"TURN: CHARACTER");
    u8g2.drawStr(0,52,"CLICK: NEXT");
    u8g2.drawStr(0,62,"HOLD: SAVE & EXIT");
  }
  drawSdActionOverlay();
  u8g2.sendBuffer();
}

static uint8_t systemMenu = 0; // 0=FX, 1=CHANNEL, 2=FEEDBACK, 3=LEARN
static uint32_t encoderPressTime = 0;
static uint8_t midiCcBank = 0;
static uint8_t learnedCc[PARAM_COUNT]; // 255 means factory mapping
static bool learnArmed=false;
static uint32_t learnStartMs=0;
static constexpr uint32_t LEARN_TIMEOUT_MS=5000U;
static constexpr uint32_t LEARN_RESULT_MS=1500U;
static uint32_t learnResultUntilMs=0;
static char learnResult[28]={0};
static void finishLearn(const char *message) {
  learnArmed=false;
  learnResultUntilMs=millis()+LEARN_RESULT_MS;
  snprintf(learnResult,sizeof(learnResult),"%s",message);
}
static bool suppressEncoderShort=false;
struct FluxCcMap { uint8_t cc; uint8_t param; };
static const FluxCcMap FLUX_CC_MAIN[] = {
  {106,TIME}, {109,FEEDBACK}, {110,DRIVE}, {111,MODE_PARAM},
  {112,BYPASS_PARAM}, {115,STEREO_MODE_PARAM}, {118,LFO_RATE},
  {119,LFO_AMOUNT}, {75,SYNC_ENABLE}, {76,SYNC_DIV},
  {86,STEREO_PHASE}, {89,BBD_MODEL}, {90,BBD_TONE},
  {77,FB_COLOR}, {78,BBD_SAT}, {30,INT_BPM},
  // RTAL-only controls; unused CCs in the supplied Phaser bank-0 chart.
  {28,MIX}, {52,OUTPUT_LEVEL}, {102,LFO_WAVE}, {103,LFO_SMOOTH},
  {105,RESET_START}, {29,BBD_NOISE}, {23,STEREO_WIDTH}, {22,INPUT_PARAM}
};
static uint8_t effectiveCc(uint8_t param) {
  if (learnedCc[param] <= 127) return learnedCc[param];
  for (const FluxCcMap &entry : FLUX_CC_MAIN) if (entry.param == param) return entry.cc;
  return 255;
}
static void saveLearnMap() {
  if (!sdReady) return;
  File f=SD.open("/RTAL_FLANGER/FLUXLRN.TMP",FILE_WRITE);
  if (!f) {showNotice("LEARN SAVE ERROR");return;}
  uint8_t hdr[4]={'F','L',1,PARAM_COUNT};
  bool ok=f.write(hdr,4)==4 && f.write(learnedCc,PARAM_COUNT)==PARAM_COUNT;
  f.close();
  if(!ok){SD.remove("/RTAL_FLANGER/FLUXLRN.TMP");showNotice("LEARN SAVE ERROR");return;}
  SD.remove("/RTAL_FLANGER/FLUXLRN.CFG");
  if(!SD.rename("/RTAL_FLANGER/FLUXLRN.TMP","/RTAL_FLANGER/FLUXLRN.CFG")) showNotice("LEARN SAVE ERROR");
}
static void loadLearnMap() {
  memset(learnedCc,255,sizeof(learnedCc));
  if(!sdReady)return;
  File f=SD.open("/RTAL_FLANGER/FLUXLRN.CFG",FILE_READ);
  if(!f)return;
  uint8_t hdr[4];
  bool ok=f.size()==4+PARAM_COUNT && f.read(hdr,4)==4 &&
    hdr[0]=='F' && hdr[1]=='L' && hdr[2]==1 && hdr[3]==PARAM_COUNT &&
    f.read(learnedCc,PARAM_COUNT)==PARAM_COUNT;
  f.close();
  if(!ok)memset(learnedCc,255,sizeof(learnedCc));
  // Validate against CURRENT effective assignments, not the immutable factory list.
  // A parameter may always return to its own original CC.
  for(uint8_t i=0;i<PARAM_COUNT;++i) {
    if(learnedCc[i]==32 || learnedCc[i]>127) learnedCc[i]=255;
    for(uint8_t ap=0;ap<ADV_COUNT;++ap)
      if(learnedCc[i]==ADV_MIDI_CC[ap]) learnedCc[i]=255;
  }
  for(uint8_t i=0;i<PARAM_COUNT;++i) {
    if(learnedCc[i]==255) continue;
    const uint8_t cc=learnedCc[i];
    bool conflict=false;
    for(uint8_t j=0;j<PARAM_COUNT;++j) {
      if(i!=j && effectiveCc(j)==cc) {conflict=true;break;}
    }
    if(conflict) learnedCc[i]=255;
  }
}
static void restoreLearnDefaults() {
  memset(learnedCc,255,sizeof(learnedCc));
  saveLearnMap();showNotice("LEARN DEFAULTS");
}
static void sendMidiFeedback(uint8_t param) {
  if (!midiFeedback || midiChannel == 0 || midiCcBank != 0) return;
  const uint8_t cc=effectiveCc(param);
  if(cc>127)return;
  const uint8_t lo=paramMin(param), hi=paramMax(param);
  const uint8_t ccval=(uint32_t(value[param]-lo)*127U+(hi-lo)/2U)/(hi-lo);
  Serial2.write(uint8_t(0xB0 | (midiChannel-1)));
  Serial2.write(cc);
  Serial2.write(ccval);
}
static void saveMidiSettings() {
  if (!sdReady) return;
  File f=SD.open("/RTAL_FLANGER/FLUXMIDI.TMP", FILE_WRITE);
  if(!f) return;
  const uint8_t b[4]={'M','F',midiChannel,uint8_t(midiFeedback?1:0)};
  f.write(b,4);f.close();SD.remove("/RTAL_FLANGER/FLUXMIDI.CFG");SD.rename("/RTAL_FLANGER/FLUXMIDI.TMP","/RTAL_FLANGER/FLUXMIDI.CFG");
}
static void loadMidiSettings() {
  if (!sdReady) return;
  File f=SD.open("/RTAL_FLANGER/FLUXMIDI.CFG",FILE_READ);
  if (!f) return;
  uint8_t b[4];const bool ok=f.size()==4 && f.read(b,4)==4;f.close();
  if(ok && b[0]=='M' && b[1]=='F' && b[2]<=16 && b[3]<=1) {midiChannel=b[2];midiFeedback=b[3]!=0;}
}
static uint8_t midiRunningStatus=0, midiData1=0, midiDataCount=0;
static bool midiInSysEx=false;
static bool midiChanged=false;
// FIX4: coalesce rapid Program Changes; do not perform SD I/O in MIDI parser.
static uint8_t pendingPcSlot=0;
static uint32_t pendingPcLastMs=0;
static uint16_t pendingPcCount=0;
static constexpr uint32_t PC_SETTLE_MS=80;
static constexpr uint32_t PC_SEQUENCE_SETTLE_MS=200;
static uint8_t pendingPcLastRaw=255;
static uint8_t pendingPcDirection=0; // 0=unknown, 1=up, 2=down
static bool pendingPcSequence=false;
static uint32_t pendingPcMinGap=0xFFFFFFFFUL,pendingPcMaxGap=0;
static uint32_t pendingPcFirstMs=0;

static uint8_t midiExpected(uint8_t status) {
  const uint8_t hi=status & 0xF0;
  return (hi==0xC0 || hi==0xD0) ? 1 : 2;
}
static void midiMessage(uint8_t status,uint8_t d1,uint8_t d2) {
  if (midiChannel != 0 && (status & 0x0F) != midiChannel-1) return;
  switch (status & 0xF0) {
    case 0xB0: {
      if(d1==32){ midiCcBank=d2; break; }
      // Learning intercepts the next CC without changing its parameter value.
      if(uiState==UI_EDIT && systemMenu==3 && learnArmed && d1!=32) {
        // CC32 is handled above; all other CCs may be learned if free.
        // In particular the selected parameter can reclaim its own factory CC.
        bool inUse=false;
        for(uint8_t i=0;i<PARAM_COUNT;++i) {
          if(i!=selected && effectiveCc(i)==d1) {inUse=true;break;}
        }
        for(uint8_t ap=0;ap<ADV_COUNT;++ap) {
          if(ADV_MIDI_CC[ap]==d1) {inUse=true;break;}
        }
        if(inUse) {finishLearn("CC IN USE");midiChanged=true;break;}
        learnedCc[selected]=d1;
        saveLearnMap();finishLearn("CC LEARNED");midiChanged=true;
        break;
      }
      if(midiCcBank==1){morphBank1CC(d1,d2);midiChanged=true;break;}
      if(midiCcBank!=0)break;
      bool advancedMatched=false;
      for(uint8_t ap=0;ap<ADV_COUNT;++ap){
        if(d1!=ADV_MIDI_CC[ap])continue;
        advancedMatched=true;
        // CC15 controller is restricted to 0..12. Decode all four matrix modes
        // from that range; preserve the internal 0..3 preset/menu representation.
        // 0..3 NORMAL, 4..6 CROSS, 7..9 PINGPONG, 10..12 DIFFERENCE.
        const uint8_t v=(ap==A_MATRIX)
          ? uint8_t(d2<=3?0:(d2<=6?1:(d2<=9?2:3)))
          : uint8_t((uint16_t(d2)*advMax(ap)+63U)/127U);
        if(adv[ap]!=v){adv[ap]=v;applyAdvanced(ap);midiChanged=true;}
        break;
      }
      if(advancedMatched)break;
      // Exactly one effective CC per parameter: a learned CC replaces the factory CC.
      // Compare only effective mappings, so the former factory CC is inactive.
      uint8_t p=255;
      for(uint8_t i=0;i<PARAM_COUNT;++i) {
        if(effectiveCc(i)==d1) {p=i;break;}
      }
      if(p!=255){
        const uint8_t lo=paramMin(p),hi=paramMax(p);
        const uint8_t v=lo+(uint16_t(d2)*(hi-lo)+63U)/127U;
        if(value[p]!=v){value[p]=v;applyValue(p);midiChanged=true;}
      }
      break;
    }
    case 0xC0:
      if(d1<128){
        const uint32_t now=millis();
        if(pendingPcCount){
          const uint32_t gap=now-pendingPcLastMs;
          if(gap<pendingPcMinGap)pendingPcMinGap=gap;
          if(gap>pendingPcMaxGap)pendingPcMaxGap=gap;
          const uint8_t direction=(d1==(uint8_t)(pendingPcLastRaw+1))?1:
                                  ((pendingPcLastRaw>0&&d1==pendingPcLastRaw-1)?2:0);
          if(direction && (!pendingPcDirection||pendingPcDirection==direction)){
            pendingPcDirection=direction;pendingPcSequence=true;
          }else if(!direction)pendingPcDirection=0;
        }else pendingPcFirstMs=now;
        pendingPcLastRaw=d1;pendingPcSlot=d1+1;pendingPcLastMs=now;
        if(pendingPcCount<65535)++pendingPcCount;
      }
      break;
    default:break;
  }
}
static void readMidi() {
  uint8_t budget=96;
  while(budget-- && Serial2.available()>0) {
    const uint8_t b=(uint8_t)Serial2.read();
    if(b>=0xF8) { // MIDI realtime may appear anywhere, even within SysEx.
      switch(b) {
        case 0xF8:{const uint32_t tickUs=micros();midiClock.clockTick(tickUs);updateExtBpmDisplayTick(tickUs);break;}
        case 0xFA:midiClock.start();if(value[SYNC_ENABLE] && value[RESET_START])audio.requestLfoPhaseReset();break;
        case 0xFB:midiClock.cont();break;
        case 0xFC:midiClock.stop();break;
        default:break;
      }
      continue;
    }
    if(b==0xF0){midiInSysEx=true;midiRunningStatus=0;midiDataCount=0;continue;}
    if(b==0xF7){midiInSysEx=false;midiRunningStatus=0;midiDataCount=0;continue;}
    if(midiInSysEx)continue;
    if(b&0x80){
      midiDataCount=0;
      if(b>=0xF0){midiRunningStatus=0;continue;}
      midiRunningStatus=b;
      continue;
    }
    if(!midiRunningStatus)continue;
    if(midiExpected(midiRunningStatus)==1){
      midiMessage(midiRunningStatus,b,0);
    } else if(midiDataCount==0){
      midiData1=b;midiDataCount=1;
    } else {
      midiMessage(midiRunningStatus,midiData1,b);
      midiDataCount=0;
    }
  }
}

static void updateSyncRate(bool force=false) {
  const bool sync = value[SYNC_ENABLE] != 0;
  clockExternal = sync && midiClock.active(micros());
  if (!sync) return; // Free-running 0.05..50Hz controlled by LFO RATE.
  const float bpm = clockExternal ? midiClock.bpm() : (float)value[INT_BPM];
  const float hz = bpm / (60.0f * DIV_BEATS[value[SYNC_DIV]]);
  if (force || fabsf(hz - audio.effectiveRateHz()) > 0.002f)
    audio.setEffectiveRateHz(hz);
}

static int feedbackPercent() {
  const float pct = 100.0f * audio.feedbackGain();
  return (int)(pct + (pct < 0.0f ? -0.5f : 0.5f));
}

// F008C: Phaser-style hierarchical encoder UI. DSP and SD formats untouched.
// UI state must be declared before midiMessage() uses it.

static uint8_t uiMenu=0, uiItem=0;
static const char* const MENU_NAMES[]={"FLANGER","LFO 1","BBD","MIDI CLOCK","STEREO","MIDI","PRESETS","SYSTEM","LFO 2","ADVANCED","MOTION FX","DSP ENGINE","MORPH","MORPH PLAY","MORPH FAV"};
// Alphabetical main-menu display order; internal uiMenu IDs stay unchanged.
static const uint8_t MAIN_MENU_ORDER[15]={9,2,11,0,1,8,5,3,10,12,14,13,6,4,7};
static uint8_t mainMenuPosition=3; // FLANGER is the initial selection
static const uint8_t MENU_COUNTS[]={6,5,5,3,5,3,1,1,4,2,4,5,7,6,3};
static const uint8_t MENU_PARAMS[5][6]={
 {TIME,FEEDBACK,MIX,DRIVE,MODE_PARAM,BYPASS_PARAM},
 {LFO_RATE,LFO_AMOUNT,LFO_WAVE,LFO_SMOOTH,RESET_START,0},
 {BBD_MODEL,BBD_TONE,BBD_SAT,BBD_NOISE,FB_COLOR,0},
 {SYNC_ENABLE,SYNC_DIV,INT_BPM,0,0,0},
 {STEREO_MODE_PARAM,STEREO_WIDTH,STEREO_PHASE,INPUT_PARAM,OUTPUT_LEVEL,0}
};
// FIX9: virtual BACK entry is the last item of every group.
static uint8_t menuItemCount(){return MENU_COUNTS[uiMenu]+1;}
static bool menuBackSelected(){return uiItem==MENU_COUNTS[uiMenu];}
static uint8_t menuParam(){return MENU_PARAMS[uiMenu][uiItem];}
static uint8_t advancedMenuParam(){return uiMenu==8?uiItem:(uiMenu==9?(uiItem==0?A_CROSS:A_CHARACTER):(uiMenu==10?(A_DUAL+uiItem):(A_TZ+uiItem)));}
static const char* const ADV_NAMES[]={"LFO2 RATE","LFO2 DEPTH","LFO2 WAVE","LFO2 TARGET","X-FEEDBACK","CHARACTER","DUAL BBD","ENV FOLLOW","FB DAMP","ST MOTION","THRU ZERO","FB MATRIX","ENV ATTACK","ENV RELEASE","ENV TARGET"};
static void formatAdvanced(char *b,size_t n,uint8_t p){
 const uint8_t v=adv[p];
 if(p==A_RATE)snprintf(b,n,"LFO2: %.2f Hz",0.05f*powf(1000.0f,v*0.01f));
 else if(p==A_WAVE){static const char* const w[]={"SINE","TRIANGLE","SQUARE","SAW"};snprintf(b,n,"WAVE: %s",w[v]);}
 else if(p==A_TARGET){static const char* const t[]={"OFF","TIME","FEEDBACK","WIDTH"};snprintf(b,n,"TARGET: %s",t[v]);}
 else if(p==A_MATRIX){static const char* const t[]={"NORMAL","CROSS","PINGPONG","DIFFERENCE"};snprintf(b,n,"MATRIX: %s",t[v]);}
 else if(p==A_ENV_TARGET){static const char* const t[]={"TIME","FEEDBACK","WIDTH","MIX"};snprintf(b,n,"ENV TO: %s",t[v]);}
 else if(p==A_CHARACTER){static const char* const t[]={"CLASSIC","MODERN","EXPERIMENT"};snprintf(b,n,"CHAR: %s",t[v]);}
 else snprintf(b,n,"%s: %u%%",ADV_NAMES[p],v);
}
static void menuSystemSelection(){
 if(uiMenu==5){systemMenu=uiItem==0?1:(uiItem==1?2:3);}
 else systemMenu=0;
}
// FIX5: Values reflect the actual DSP mapping, not arbitrary descriptive scales.
static const char* intensityName(uint8_t v){return v<20?"MINIMAL":v<40?"LIGHT":v<60?"MEDIUM":v<80?"STRONG":"MAXIMUM";}
static void formatParam(char* b,size_t n,uint8_t p){
 const unsigned v=value[p];
 if(p==LFO_WAVE) snprintf(b,n,"WAVE: %s",WAVE_NAMES[v]);
 else if(p==SYNC_DIV) snprintf(b,n,"DIV: %s",DIV_NAMES[v]);
 else if(p==MODE_PARAM) snprintf(b,n,"MODE: %s",v?"CHORUS":"FLANGER");
 else if(p==INPUT_PARAM) snprintf(b,n,"INPUT: %s",v==0?"LEFT":v==1?"RIGHT":"SUM");
 else if(p==SYNC_ENABLE) snprintf(b,n,"SYNC: %s",v?"MIDI CLOCK":"INTERNAL");
 else if(p==RESET_START) snprintf(b,n,"RESET: %s",v?"ON":"OFF");
 else if(p==BYPASS_PARAM||p==STEREO_MODE_PARAM) snprintf(b,n,"%s: %s",LABELS[p],v?"ON":"OFF");
 else if(p==LFO_RATE) snprintf(b,n,"RATE: %.2f Hz",0.05f*powf(1000.0f,v*0.01f));
 else if(p==BBD_TONE) snprintf(b,n,"TONE: %.1f kHz",(1800.0f+70.0f*v)/1000.0f);
 else if(p==BBD_MODEL) snprintf(b,n,"BBD: %u%% %s",v,intensityName(v));
 else if(p==LFO_SMOOTH) snprintf(b,n,"SMOOTH: %.1f ms",0.3f*powf(240.0f,v*0.01f));
 else if(p==DRIVE) snprintf(b,n,"DRIVE: %+.1f dB",-7.0f+0.35f*v);
 else if(p==OUTPUT_LEVEL) snprintf(b,n,"OUTPUT: %+.1f dB",-12.0f+0.24f*v);
 else if(p==STEREO_PHASE) snprintf(b,n,"ST PHASE: %u deg",(unsigned)(v*1.8f+0.5f));
 else if(p==INT_BPM) snprintf(b,n,"BPM: %u",v);
 else if(p==TIME) snprintf(b,n,"TIME: %u%%",v); // LFO depth and mode affect true delay
 else snprintf(b,n,"%s: %u%%",LABELS[p],v);
}
static void drawMenuUi(){
 u8g2.clearBuffer();u8g2.setFont(u8g2_font_5x7_tf);
 u8g2.drawStr(0,7,"HQ FLANGER V1.0 RC3 FIX3");u8g2.drawHLine(0,9,128);
 char b[48];
 if(uiState==UI_MENU){
  u8g2.drawStr(0,18,"SELECT GROUP");
  for(uint8_t k=0;k<4;++k){uint8_t pos=(mainMenuPosition/4)*4+k;
   if(pos>15)break;
   const uint8_t idx=(pos==15)?15:MAIN_MENU_ORDER[pos];
   const char* name=(pos==15)?"BACK":MENU_NAMES[idx];
   snprintf(b,sizeof(b),"%c %s",pos==mainMenuPosition?'>':' ',name);u8g2.drawStr(0,29+10*k,b);}
 }else{
  snprintf(b,sizeof(b),"%s / %s",MENU_NAMES[uiMenu],uiState==UI_EDIT?"EDIT":"SELECT");u8g2.drawStr(0,18,b);
  if(uiMenu<5 || (uiMenu>=8 && uiMenu<=11)){
   for(int8_t k=-1;k<=2;++k){int ix=(int)uiItem+k;if(ix<0||ix>=menuItemCount())continue;
    if(ix==MENU_COUNTS[uiMenu])snprintf(b,sizeof(b),"BACK");
    else if(uiMenu>=8)formatAdvanced(b,sizeof(b),uiMenu==8?ix:(uiMenu==9?(ix==0?A_CROSS:A_CHARACTER):(uiMenu==10?(A_DUAL+ix):(A_TZ+ix))));
    else formatParam(b,sizeof(b),MENU_PARAMS[uiMenu][ix]);char line[48];snprintf(line,sizeof(line),"%c%s",k==0?'>':' ',b);u8g2.drawStr(0,38+9*k,line);}
  }else if(uiMenu==12){
   if(menuBackSelected())snprintf(b,sizeof(b),"> BACK");
   else if(uiItem==0)snprintf(b,sizeof(b),"> A PRESET: %03u",(unsigned)morphSlotA);
   else if(uiItem==1)snprintf(b,sizeof(b),"> B PRESET: %03u",(unsigned)morphSlotB);
   else if(uiItem==2)snprintf(b,sizeof(b),"> LOAD A+B (CLICK)");
   else if(uiItem==3)snprintf(b,sizeof(b),"> POSITION: %u%%",(unsigned)morphPosition);
   else if(uiItem==4)snprintf(b,sizeof(b),"> TIME: %u%%",(unsigned)morphTime);
   else if(uiItem==5)snprintf(b,sizeof(b),"> MODE: %s",morphMode?"AUTO":"MANUAL");
   else snprintf(b,sizeof(b),"> CURVE: %s",morphCurve?"SMOOTH":"LINEAR");
   u8g2.drawStr(0,35,b);
   // Display both names from the same cache used by the preset browser.
   // Avoid SD I/O on subsequent screen redraws.
   if(sdReady && !browserNamesLoaded)presetBrowserCacheLoad();
   char aName[20],bName[20];
   if(browserNamesLoaded){
     snprintf(aName,sizeof(aName),"%s",browserNames[morphSlotA-1]);
     snprintf(bName,sizeof(bName),"%s",browserNames[morphSlotB-1]);
   }else{
     snprintf(aName,sizeof(aName),"PRESET %03u",(unsigned)morphSlotA);
     snprintf(bName,sizeof(bName),"PRESET %03u",(unsigned)morphSlotB);
   }
   snprintf(b,sizeof(b),"A%03u: %.19s",(unsigned)morphSlotA,aName);u8g2.drawStr(0,48,b);
   snprintf(b,sizeof(b),"B%03u: %.19s",(unsigned)morphSlotB,bName);u8g2.drawStr(0,58,b);
  }else if(uiMenu==13){
   if(menuBackSelected())snprintf(b,sizeof(b),"> BACK");
   else if(uiItem==0)snprintf(b,sizeof(b),"> PINGPONG: %s",morphPingPong?"ON":"OFF");
   else if(uiItem==1)snprintf(b,sizeof(b),"> SYNC: %s",morphSync?"ON":"OFF");
   else if(uiItem==2){static const char* const d[]={"1/16 BAR","1/8 BAR","1/4 BAR","1/2 BAR","1 BAR","2 BARS","4 BARS","8 BARS"};snprintf(b,sizeof(b),"> DIV: %s",d[morphDivision]);}
   else if(uiItem==3)snprintf(b,sizeof(b),"> START (CLICK)");
   else if(uiItem==4)snprintf(b,sizeof(b),"> STOP (CLICK)");
   else snprintf(b,sizeof(b),"> REVERSE: %s",morphReverse?"ON":"OFF");
   u8g2.drawStr(0,35,b);
   u8g2.drawStr(0,49,morphRunning?"PLAY: RUNNING":"PLAY: STOPPED");
   u8g2.drawStr(0,60,morphSync?"SYNC: MIDI CLOCK":"SYNC: INTERNAL");
  }else if(uiMenu==14){
   if(menuBackSelected())snprintf(b,sizeof(b),"> BACK");
   else if(uiItem==0)snprintf(b,sizeof(b),"> FAVORITE: %02u/16",(unsigned)(morphFavorite+1));
   else if(uiItem==1)snprintf(b,sizeof(b),"> SAVE PAIR (CLICK)");
   else snprintf(b,sizeof(b),"> LOAD PAIR (CLICK)");
   u8g2.drawStr(0,35,b);
   snprintf(b,sizeof(b),"A:%03u B:%03u",(unsigned)morphSlotA,(unsigned)morphSlotB);u8g2.drawStr(0,49,b);
   u8g2.drawStr(0,60,morphFavValid[morphFavorite]?"FAVORITE: STORED":"FAVORITE: EMPTY");
  }else if(uiMenu==5){
   if(menuBackSelected())snprintf(b,sizeof(b),"BACK");
   else if(uiItem==0)snprintf(b,sizeof(b),"CHANNEL: %s",midiChannel==0?"OMNI":"CH 1-16");
   else if(uiItem==1)snprintf(b,sizeof(b),"FEEDBACK: %s",midiFeedback?"ON":"OFF");
   else if(learnArmed)snprintf(b,sizeof(b),"LEARN WAIT CC...");
   else if((int32_t)(learnResultUntilMs-millis())>0)snprintf(b,sizeof(b),"%s",learnResult);
   else snprintf(b,sizeof(b),"%s CC:%u",LABELS[selected],(unsigned)effectiveCc(selected));
   u8g2.drawStr(0,35,b);
   if(uiItem==0&&midiChannel){snprintf(b,sizeof(b),"CURRENT: CH %u",midiChannel);u8g2.drawStr(0,45,b);}
   u8g2.drawStr(0,60,"1S=UP  3S=HOME");
  }else if(uiMenu==6){
   if(menuBackSelected())u8g2.drawStr(0,35,"> BACK");
   else {u8g2.drawStr(0,35,"> PRESETS 001-128");u8g2.drawStr(0,49,"PRESS: BROWSE");}
  }else{
   if(menuBackSelected())u8g2.drawStr(0,35,"> BACK");
   else {u8g2.drawStr(0,28,"PHOENIX ESP32-S3");u8g2.drawStr(0,37,sdReady?"SD: READY":"SD: NOT READY");
    snprintf(b,sizeof(b),"DSP %lu/%lu us",(unsigned long)audio.avgLoadUs(),(unsigned long)audio.peakLoadUs());u8g2.drawStr(0,47,b);
    snprintf(b,sizeof(b),"XRUN %lu",(unsigned long)audio.underruns());u8g2.drawStr(0,57,b);
   }
  }
 }
 drawSdActionOverlay();
 u8g2.sendBuffer();
}

// FIX11: Motion trace uses actual DSP delay telemetry, not a second simulated LFO.
// Rendering is confined to the main loop; no audio callback or timer work.
static void drawMotionHome(){
  const float base=audio.nominalDelayMs();
  const float actual=audio.actualDelayMs();
  float deviation=actual-base;
  if(deviation>10.0f)deviation=10.0f;
  if(deviation< -10.0f)deviation=-10.0f;
  int y=27-(int)(deviation*1.1f);
  if(y<15)y=15;if(y>39)y=39;
  motionTrace[motionHead]=(uint8_t)y;
  motionHead=(uint8_t)((motionHead+1)%112);
  if(!motionTraceReady && motionHead==0)motionTraceReady=true;
  u8g2.clearBuffer();u8g2.setFont(u8g2_font_5x7_tf);
  char b[48], nm[20];
  if(activePreset>=1 && activePreset<=128 && browserNamesLoaded)
    snprintf(nm,sizeof(nm),"%s",browserNames[activePreset-1]);
  else presetName(activePreset,nm,sizeof(nm));
  drawScrollingPresetHeader(nm);u8g2.drawHLine(0,10,128);
  u8g2.drawStr(0,18,"MOTION");
  u8g2.drawHLine(8,27,112);
  const int n=motionTraceReady?112:motionHead;
  for(int i=1;i<n;i++){
    const int a=(motionHead+112-n+i-1)%112;
    const int c=(motionHead+112-n+i)%112;
    u8g2.drawLine(8+i-1,motionTrace[a],8+i,motionTrace[c]);
  }
  snprintf(b,sizeof(b),"BASE %.2f ACT %.2fms",(double)base,(double)actual);
  u8g2.drawStr(0,48,b);
  snprintf(b,sizeof(b),"LFO %s %u%%",WAVE_NAMES[value[LFO_WAVE]],(unsigned)value[LFO_AMOUNT]);
  u8g2.drawStr(0,56,b);
  snprintf(b,sizeof(b),"FB %u%% MIX %u%%",(unsigned)value[FEEDBACK],(unsigned)value[MIX]);
  u8g2.drawStr(0,63,b);
  drawSdActionOverlay();u8g2.sendBuffer();
}

static void drawUI() {
  if(uiState==UI_PRESET_BROWSER||uiState==UI_PRESET_ACTION||uiState==UI_PRESET_RENAME){drawPresetBrowser();return;}
  if(uiState!=UI_HOME){drawMenuUi();return;}
  if(motionHome){drawMotionHome();return;}
  // FIX3: Phaser-style performance home page. Encoder directly edits TIME.
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  char b[48];
  // RC2: top row displays preset number/name; graph and DSP telemetry stay intact.
  char currentName[20];
  if(activePreset>=1 && activePreset<=128 && browserNamesLoaded)snprintf(currentName,sizeof(currentName),"%s",browserNames[activePreset-1]);
  else presetName(activePreset,currentName,sizeof(currentName));
  drawScrollingPresetHeader(currentName);
  u8g2.drawHLine(0,10,128);
  // TIME is the Cluster Flux counterpart to Phaser SWEEP.
  // Simple moving delay-time marker, no extra display/audio tasks.
  const uint8_t timePct=value[TIME];
  const int pos=8+(int)((uint32_t)timePct*110U/100U);
  // FIX11: five rows with tighter vertical spacing, keeping clear gap above TIME.
  // Keep the entire graph above the TIME text (baseline y=44).
  for(int x=8;x<120;x+=3){
    const int d=abs(x-pos);
    u8g2.drawPixel(x,14+(d%5)*4); // five rows: y=14,18,22,26,30; clear of TIME text
  }
  u8g2.drawVLine(pos,13,20); // marker y=13..32, safe gap before text at y=44

  const float baseMs=audio.nominalDelayMs();
  const float actualMs=audio.actualDelayMs();
  // BASE is the calculated delay centre; ACT is the current left-channel
  // DSP tap, including LFO modulation and smoothing (audio-thread telemetry).
  snprintf(b,sizeof(b),"TIME %3u%% BASE %4.1fms",(unsigned)timePct,(double)baseMs);
  u8g2.drawStr(0,44,b);
  snprintf(b,sizeof(b),"ACT L %5.2fms",(double)actualMs);
  u8g2.drawStr(0,52,b);
  const bool clockSeen=midiClock.ticks()>0;
  const bool clockLost=clockSeen && (uint32_t)(micros()-extBpmLastTickUs)>2000000U;
  if(value[SYNC_ENABLE] && clockLost){
    snprintf(b,sizeof(b),"SYNC:EXT %s ---",DIV_NAMES[value[SYNC_DIV]]);
  }else if(value[SYNC_ENABLE] && clockExternal && extBpmValid){
    snprintf(b,sizeof(b),"SYNC:EXT %s %uBPM",DIV_NAMES[value[SYNC_DIV]],extBpmShown);
  }else{
    const unsigned bpmDisp=(unsigned)((clockExternal?midiClock.bpm():value[INT_BPM])+0.5f);
    snprintf(b,sizeof(b),"SYNC:%s %s %uBPM",value[SYNC_ENABLE]?(clockExternal?"EXT":"INT"):"OFF",
             DIV_NAMES[value[SYNC_DIV]],bpmDisp);
  }
  u8g2.drawStr(0,61,b);
  drawSdActionOverlay();
  u8g2.sendBuffer();
}

// FIX6: non-invasive PSRAM and preset storage footprint audit.
// No preset data is preloaded and no DSP, MIDI, or SD format is changed.
static void printPresetMemoryAudit(){
  const size_t mainBytes=sizeof(FluxPreset);
  const size_t advBytes=4U+ADV_COUNT;
  const size_t morphBytes=14U;
  const size_t totalBytes=128U*(mainBytes+advBytes+morphBytes);
  Serial.printf("PRESET MEMORY: MAIN=%u ADV=%u MORPH=%u bytes/slot; 128 slots=%u bytes (raw maximum)\n",
    (unsigned)mainBytes,(unsigned)advBytes,(unsigned)morphBytes,(unsigned)totalBytes);
  Serial.printf("MEMORY: HEAP_FREE=%u HEAP_MAX_BLOCK=%u PSRAM_PRESENT=%s PSRAM_TOTAL=%u PSRAM_FREE=%u\n",
    (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getMaxAllocHeap(),
    psramFound()?"YES":"NO",(unsigned)ESP.getPsramSize(),(unsigned)ESP.getFreePsram());
  if(!sdReady){Serial.println("PRESET FILE AUDIT: SD NOT READY");return;}
  const uint8_t slots[]={1,8,9,50,128};
  for(size_t i=0;i<sizeof(slots);++i){
    char path[48];const uint8_t slot=slots[i];
    presetPath(path,sizeof(path),slot);File f=SD.open(path,FILE_READ);
    const int32_t mainSize=f?(int32_t)f.size():-1;if(f)f.close();
    advancedPath(path,sizeof(path),slot);f=SD.open(path,FILE_READ);
    const int32_t advSize=f?(int32_t)f.size():-1;if(f)f.close();
    morphConfigPath(path,sizeof(path),slot);f=SD.open(path,FILE_READ);
    const int32_t morphSize=f?(int32_t)f.size():-1;if(f)f.close();
    Serial.printf("PRESET FILE AUDIT %03u: MAIN=%ld ADV=%ld MORPH=%ld bytes (-1=missing)\n",
      (unsigned)slot,(long)mainSize,(long)advSize,(long)morphSize);
  }
}

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\nRTAL HQ FLANGER V1.0 FINAL");
  SPI.begin(OLED_SCK,-1,OLED_MOSI,OLED_CS);
  u8g2.begin();
  u8g2.setContrast(220);
  input.begin();
  sdReady=SD.begin(SD_CS_PIN,SPI,8000000);
  if (sdReady && !SD.exists("/RTAL_FLANGER")) {
    if (!SD.mkdir("/RTAL_FLANGER")) {
      Serial.println("SD: cannot create /RTAL_FLANGER");
      showNotice("SD DIR ERROR");
      sdReady=false;
    }
  }
  loadHomeView();
  loadMidiSettings();morphLoadFavorites();
  loadLearnMap();
  Serial.printf("SD:%s\n",sdReady?"OK":"ERROR");
  Serial2.begin(31250, SERIAL_8N1, MIDI_DIN_RX, MIDI_DIN_TX);
  Serial.println("MIDI: CH1 CC32=0 main bank; Phaser-compatible CC map; PC0..127; MIDI clock");
  for (uint8_t p=0;p<PARAM_COUNT;++p) applyValue(p);
  audio.setChorus(chorus);
  audio.setBypass(bypass);
  audio.setEnhanced(enhanced);
  audio.setInputSource(inputSource);
  audio.begin();
  applyAllAdvanced();
  updateSyncRate(true);
  u8g2.clearBuffer();
  u8g2.drawXBMP(0,0,128,64,RTAL_FLANGER_SPLASH);
  u8g2.sendBuffer();
  delay(500);
  // FIX7: load compact name index; first boot imports legacy files with progress.
  const uint32_t nameLoadStart = millis();
  presetBrowserCacheLoad();
  const uint32_t morphIndexStart=millis();
  morphConfigFlagsLoad();
  Serial.printf("MORPH MARKERS: 128 cached in %lu ms\n",(unsigned long)(millis()-morphIndexStart));
  Serial.printf("PRESET NAME CACHE: %u names in %lu ms (SD:%s)\n",
                128U, (unsigned long)(millis()-nameLoadStart),
                sdReady ? "OK" : "NOT READY");
  printPresetMemoryAudit();
  presetRamInit();
  drawBootProgress(128,128,"INITIALIZATION READY");
  drawUI();
  Serial.printf("Audio:%s fs=%lu block=%u I2S 32bit R/L, DIN RX=%u @31250\n",
                audio.ok()?"OK":"ERROR",(unsigned long)audio.sampleRate(),
                (unsigned)audio.blockFrames(),(unsigned)MIDI_DIN_RX);
  if(!audio.ok()) Serial.println("ERROR - check I2S driver/pins and board settings");
}

void loop() {
  midiChanged=false;
  // FIX9: sample the encoder switch before MIDI/encoder processing.
  // This ensures hold timing starts at the beginning of the loop.
  const uint32_t holdSampleMs = millis();
  const bool holdSampleDown = (digitalRead(ENC_SW_PIN) == LOW);
  readMidi();
  // Only the last PC of a burst is loaded; keep MIDI parsing independent of SD.
  if(pendingPcSlot && (uint32_t)(millis()-pendingPcLastMs)>=
      (pendingPcSequence?PC_SEQUENCE_SETTLE_MS:PC_SETTLE_MS)){
    const uint8_t slot=pendingPcSlot;
    const uint16_t count=pendingPcCount;
    const uint32_t minGap=(pendingPcCount>1)?pendingPcMinGap:0;
    const uint32_t maxGap=(pendingPcCount>1)?pendingPcMaxGap:0;
    const uint32_t span=pendingPcLastMs-pendingPcFirstMs;
    pendingPcSlot=0;pendingPcCount=0;pendingPcLastRaw=255;
    pendingPcDirection=0;pendingPcSequence=false;
    pendingPcMinGap=0xFFFFFFFFUL;pendingPcMaxGap=0;
    const uint32_t loadStart=millis();
    suppressMidiPresetNotice=true;
    loadPreset(slot);
    suppressMidiPresetNotice=false;
    Serial.printf("MIDI PC BURST: %u messages; TARGET: %03u; LOAD: %lu ms; GAP: %lu..%lu ms; SPAN: %lu ms\n",
                  (unsigned)count,(unsigned)slot,(unsigned long)(millis()-loadStart),
                  (unsigned long)minGap,(unsigned long)maxGap,(unsigned long)span);
    midiChanged=true;
  }
  FluxInputState st;
  input.update(st);
  bool changed = midiChanged;

  // FIX9: prompt switch sampling and hold diagnostics. Two-stage hold. 1..3 s: one menu level up on RELEASE.
  // >=3 s: HOME immediately, with release suppressed.
  // Short clicks are also classified on release; no accidental menu entry.
  bool encoderShortClick = false;
  const uint32_t pressNow = holdSampleMs;
  const bool encoderDown = holdSampleDown;
  if (encoderDown) {
    if (!encoderPressTime) encoderPressTime = pressNow ? pressNow : 1;
    if (!suppressEncoderShort && (uint32_t)(pressNow - encoderPressTime) >= 2000U) {
      Serial.printf("ENCODER HOLD HOME after %lu ms\n", (unsigned long)(pressNow-encoderPressTime));
      if (learnArmed) finishLearn("LEARN CANCEL");
      if(uiState==UI_PRESET_RENAME)presetNameSave(browserSlot,renameText);
      uiState = UI_HOME;
      systemMenu = 0;
      suppressEncoderShort = true;
      changed = true;
    }
  } else if (encoderPressTime) {
    const uint32_t heldMs = (uint32_t)(pressNow - encoderPressTime);
    if (!suppressEncoderShort) {
      if (heldMs >= 800U) {
        if(uiState==UI_HOME){
          motionHome=!motionHome;
          motionHead=0;motionTraceReady=false;
          saveHomeView();
          Serial.printf("HOME VIEW: %s\n",motionHome?"MOTION":"CLASSIC");
          changed=true;
        } else {
        if (learnArmed) finishLearn("LEARN CANCEL");
        if (uiState == UI_PRESET_RENAME){presetNameSave(browserSlot,renameText);uiState=UI_PRESET_ACTION;}
        else if(uiState == UI_PRESET_ACTION)uiState=UI_PRESET_BROWSER;
        else if(uiState == UI_PRESET_BROWSER)uiState=UI_MENU;
        else if (uiState == UI_EDIT) uiState = UI_PAGE;
        else if (uiState == UI_PAGE) uiState = UI_MENU;
        else if (uiState == UI_MENU) uiState = UI_HOME;
        systemMenu = 0;
        changed = true;
        }
      } else if (heldMs >= 25U) {
        encoderShortClick = true;
      }
    }
    encoderPressTime = 0;
    suppressEncoderShort = false;
  }
  if(st.encoderStep){
    if(uiState==UI_PRESET_BROWSER){int v=(int)browserSlot+st.encoderStep;if(v<1)v=1;if(v>128)v=128;browserSlot=(uint8_t)v;changed=true;}
    else if(uiState==UI_PRESET_ACTION){int v=(int)browserAction+(st.encoderStep>0?1:-1);if(v<0)v=0;if(v>2)v=2;browserAction=(uint8_t)v;changed=true;}
    else if(uiState==UI_PRESET_RENAME){
      int idx=0;const int alphabetLen=(int)strlen(PRESET_ALPHABET);
      char old=renameText[renamePos]?renameText[renamePos]:' ';
      for(int i=0;i<alphabetLen;++i)if(PRESET_ALPHABET[i]==old){idx=i;break;}
      idx=(idx+(st.encoderStep>0?1:-1)+alphabetLen)%alphabetLen;
      renameText[renamePos]=PRESET_ALPHABET[idx];renameText[19]=0;changed=true;
    }
    else if(uiState==UI_HOME){int v=(int)value[TIME]+acceleratedEncoderStep(st.encoderStep,TIME); if(v<paramMin(TIME))v=paramMin(TIME); if(v>paramMax(TIME))v=paramMax(TIME); if(v!=value[TIME]){value[TIME]=v;applyValue(TIME);sendMidiFeedback(TIME);changed=true;}}
    else if(uiState==UI_MENU){int v=(int)mainMenuPosition+(st.encoderStep>0?1:-1);mainMenuPosition=(uint8_t)((v+16)%16);uiMenu=(mainMenuPosition==15)?15:MAIN_MENU_ORDER[mainMenuPosition];uiItem=0;changed=true;}
    else if(uiState==UI_PAGE){
      int count=menuItemCount();int v=(int)uiItem+(st.encoderStep>0?1:-1);
      uiItem=(v+count)%count;menuSystemSelection();changed=true;
    }else if(uiState==UI_EDIT){
      if(uiMenu==12){
        int step=st.encoderStep>0?1:-1;
        if(uiItem==0){int v=(int)morphSlotA+step;if(v<1)v=128;if(v>128)v=1;morphSlotA=v;morphActive=false;}
        else if(uiItem==1){int v=(int)morphSlotB+step;if(v<1)v=128;if(v>128)v=1;morphSlotB=v;morphActive=false;}
        else if(uiItem==3){int v=(int)morphPosition+st.encoderStep;if(v<0)v=0;if(v>100)v=100;morphPosition=v;morphAutoPos=v;morphMode=0;morphApply();}
        else if(uiItem==4){int v=(int)morphTime+st.encoderStep;if(v<0)v=0;if(v>100)v=100;morphTime=v;}
        else if(uiItem==5){morphMode=st.encoderStep>0?1:0;morphLastMs=millis();morphAutoPos=morphPosition;}
        else if(uiItem==6){morphCurve=st.encoderStep>0?1:0;morphApply();}
        if(uiItem==0 || uiItem==1 || uiItem==3 || uiItem==4 || uiItem==5 || uiItem==6)morphConfigDirty=true;
        changed=true;
      }else if(uiMenu==13){
        if(uiItem==0)morphPingPong=st.encoderStep>0;
        else if(uiItem==1){morphSync=st.encoderStep>0;morphClockStartTick=midiClock.ticks();}
        else if(uiItem==2){int v=(int)morphDivision+(st.encoderStep>0?1:-1);morphDivision=(uint8_t)((v+8)%8);}
        else if(uiItem==5)morphReverse=st.encoderStep>0;
        if(uiItem==0 || uiItem==1 || uiItem==2 || uiItem==5)morphConfigDirty=true;
        changed=true;
      }else if(uiMenu==14){
        if(uiItem==0){int v=(int)morphFavorite+(st.encoderStep>0?1:-1);morphFavorite=(uint8_t)((v+16)%16);changed=true;}
      }else if(uiMenu>=8){
        const uint8_t p=advancedMenuParam();
        int v=(int)adv[p]+st.encoderStep;
        if(v<0)v=0;if(v>advMax(p))v=advMax(p);
        if(v!=adv[p]){morphActive=false;morphMode=0;adv[p]=v;applyAdvanced(p);changed=true;}
      }else if(uiMenu<5){
        uint8_t p=menuParam();int v=(int)value[p]+acceleratedEncoderStep(st.encoderStep,p);
        if(v<paramMin(p))v=paramMin(p);if(v>paramMax(p))v=paramMax(p);
        if(v!=value[p]){morphActive=false;morphMode=0;value[p]=v;applyValue(p);sendMidiFeedback(p);changed=true;}
      }else if(uiMenu==5){
        if(uiItem==0){int v=(int)midiChannel+st.encoderStep;if(v<0)v=0;if(v>16)v=16;
          if(v!=midiChannel){midiChannel=v;saveMidiSettings();changed=true;}}
        else if(uiItem==1){midiFeedback=st.encoderStep>0;saveMidiSettings();changed=true;}
        else{int v=(int)selected+(st.encoderStep>0?1:-1);selected=(v+PARAM_COUNT)%PARAM_COUNT;
          learnArmed=false;learnResultUntilMs=0;changed=true;}
      }
    }
  }
  if(encoderShortClick){
    if(uiState==UI_PRESET_BROWSER){uiState=UI_PRESET_ACTION;browserAction=0;changed=true;}
    else if(uiState==UI_PRESET_ACTION){
      if(browserAction==0){loadPreset(browserSlot);uiState=UI_PRESET_BROWSER;}
      else if(browserAction==1){savePreset(browserSlot);uiState=UI_PRESET_BROWSER;}
      else if(browserAction==2){presetBeginRename();}
      changed=true;
    }else if(uiState==UI_PRESET_RENAME){
      if(renamePos<18){++renamePos;renameText[renamePos]=renameText[renamePos]?renameText[renamePos]:' ';renameText[19]=0;}
      else{presetNameSave(browserSlot,renameText);uiState=UI_PRESET_ACTION;}
      changed=true;
    }else if(uiState==UI_HOME){uiState=UI_MENU;mainMenuPosition=3;uiMenu=MAIN_MENU_ORDER[mainMenuPosition];uiItem=0;changed=true;}
    else if(uiState==UI_MENU){
      if(uiMenu==15){uiState=UI_HOME;systemMenu=0;}
      else if(uiMenu==6){browserSlot=activePreset?activePreset:1;uiState=UI_PRESET_BROWSER;}
      else {uiState=UI_PAGE;uiItem=0;menuSystemSelection();}
      changed=true;
    }
    else if(uiState==UI_PAGE){
      if(menuBackSelected()){
        if(learnArmed)finishLearn("LEARN CANCEL");
        uiState=UI_MENU;systemMenu=0;changed=true;
      }
      else if(uiMenu<5){selected=menuParam();uiState=UI_EDIT;changed=true;}
      else if(uiMenu==12){if(uiItem==2)morphLoadPair();else uiState=UI_EDIT;changed=true;}
      else if(uiMenu==13){
        if(uiItem==3){morphMode=1;morphRunning=true;morphLastMs=millis();morphAutoPos=morphPosition;morphClockStartTick=midiClock.ticks();showNotice("MORPH START");}
        else if(uiItem==4){morphRunning=false;morphMode=0;showNotice("MORPH STOP");}
        else uiState=UI_EDIT;
        changed=true;
      }
      else if(uiMenu==14){
        if(uiItem==1){morphFavA[morphFavorite]=morphSlotA;morphFavB[morphFavorite]=morphSlotB;morphFavValid[morphFavorite]=true;morphSaveFavorites();showNotice("MORPH FAVORITE SAVED");}
        else if(uiItem==2){if(morphFavValid[morphFavorite]){morphSlotA=morphFavA[morphFavorite];morphSlotB=morphFavB[morphFavorite];morphLoadPair();}else showNotice("FAVORITE EMPTY");}
        else uiState=UI_EDIT;
        changed=true;
      }
      else if(uiMenu>=8){uiState=UI_EDIT;changed=true;}
      else if(uiMenu==5){uiState=UI_EDIT;menuSystemSelection();changed=true;}
      else if(uiMenu==6){browserSlot=activePreset?activePreset:1;uiState=UI_PRESET_BROWSER;changed=true;}
    }else if(uiState==UI_EDIT){
      if(uiMenu==5&&uiItem==2){
        if(learnArmed)finishLearn("LEARN CANCEL");
        else{learnArmed=true;learnStartMs=millis();learnResultUntilMs=0;}
      }else{uiState=UI_PAGE;systemMenu=0;}
      changed=true;
    }
  }
  // RC2: T1-T8 no longer load/save presets. MIDI PC0..127 remains intact.
  morphUpdate();
  const uint32_t now=millis();
  refreshExtBpmShown(now);
  if(learnArmed && (uint32_t)(now-learnStartMs)>=LEARN_TIMEOUT_MS) {
    finishLearn("LEARN TIMEOUT");
    changed=true;
  }
  if (changed || now-lastRateUpdate>=40U) {
    lastRateUpdate=now;
    updateSyncRate(changed);
  }
  if(changed || now-lastDraw >= ((uiState==UI_HOME && motionHome)?80U:250U)) { lastDraw=now; drawUI(); }
  if(RTAL_FLANGER_DEBUG_TELEMETRY && (changed || now-lastTelemetry>=2000U)) {
    lastTelemetry=now;
    Serial.printf("F008A %s %s stereo=%s in=%u time=%.2fms FB=%d%% wave=%s smooth=%u sync=%s div=%s bpm=%.1f rate=%.3fHz amt=%u%% mix=%u%% tone=%u%% sat=%u%% model=%u%% fbColor=%u%% hiss=%u%% width=%u%% phase=%udeg F8=%lu play=%u DSP=%luus peak=%luus underruns=%lu\n",
                  chorus?"CHORUS":"FLANGE",bypass?"BYPASS":"ACTIVE",enhanced?"ENH":"ORIG",
                  (unsigned)inputSource,(double)audio.nominalDelayMs(),feedbackPercent(),
                  WAVE_NAMES[value[LFO_WAVE]],(unsigned)value[LFO_SMOOTH],
                  value[SYNC_ENABLE] ? (clockExternal?"EXT":"INT") : "OFF",
                  DIV_NAMES[value[SYNC_DIV]],
                  (double)(clockExternal?midiClock.bpm():value[INT_BPM]),
                  (double)audio.effectiveRateHz(),(unsigned)value[LFO_AMOUNT],
                  (unsigned)value[MIX],(unsigned)value[BBD_TONE],(unsigned)value[BBD_SAT],
                  (unsigned)value[BBD_MODEL],(unsigned)value[FB_COLOR],
                  (unsigned)value[BBD_NOISE],(unsigned)value[STEREO_WIDTH],
                  (unsigned)value[STEREO_PHASE]*180U/100U,
                  (unsigned long)midiClock.ticks(),(unsigned)midiClock.playing(),
                  (unsigned long)audio.avgLoadUs(),(unsigned long)audio.peakLoadUs(),
                  (unsigned long)audio.underruns());
  }
  delay(2);
}
