// V2.ino
// Contrôleur Audio DIY - Pro Micro
// - 3 potentiomètres : A0, A1, A2 -> envoi périodique sur le port série sous forme "POTS:v1|v2|v3|20"
// - 3 boutons programmables (pins 3,2,4) : actions assignables par commande série (modes 0/1/2)
// - 2 boutons poussoirs (gauche/droite) pins 5 et 6 ; leds indicatrices par défaut pins 7 et 8
// - ruban LED de 3 leds (WS2812) sur pin configurable (par défaut 16)
// - configuration modifiable via protocole série ASCII et persistée dans EEPROM

#include <Arduino.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>

// Hardware default pins
const uint8_t POT_PINS[3] = {A0, A1, A2};
const uint8_t MAIN_BTN_PINS[3] = {3, 2, 4}; // boutons programmables
const uint8_t LEFT_BTN_PIN = 5;  // bouton poussoir gauche
const uint8_t RIGHT_BTN_PIN = 6; // bouton poussoir droite

// EEPROM-stored configuration
struct Config {
  uint32_t magic; // signature
  uint8_t version;
  uint8_t ledPin;       // data pin for WS2812
  uint8_t indLeftPin;   // indicator LED left
  uint8_t indLeftActiveHigh;
  uint8_t indRightPin;  // indicator LED right
  uint8_t indRightActiveHigh;
  uint8_t maps[3][3];   // mode (0..2) x button (0..2) -> action code
  uint8_t colors[3][3]; // 3 leds, RGB each
  uint8_t potsEnabled;  // 1 = send pots periodically, 0 = disabled
};

const uint32_t CFG_MAGIC = 0xC0FFEE42;
const uint8_t CFG_VERSION = 3;
const int EEPROM_ADDR = 0;

Config cfg;

// NeoPixel
#define NUM_LEDS 3
Adafruit_NeoPixel *strip = nullptr;

// Runtime state
uint16_t lastPots[3] = {0,0,0};
unsigned long lastPotSend = 0;
const unsigned long POT_SEND_INTERVAL = 200; // ms

// Debounce
unsigned long lastDebounceTime[5];
const unsigned long DEBOUNCE_MS = 30;
bool lastBtnStateMain[3];
bool lastBtnStateLeft = false;
bool lastBtnStateRight = false;

// Action codes
enum ActionCode : uint8_t {
  ACT_NONE = 0,
  ACT_PREV = 1,
  ACT_PLAYPAUSE = 2,
  ACT_NEXT = 3,
  ACT_STOP = 4,
  ACT_CUSTOM1 = 10,
  ACT_CUSTOM2 = 11,
  ACT_CUSTOM3 = 12
};

// Helper to map action code to string
const char* actionName(uint8_t code) {
  switch(code) {
    case ACT_PREV: return "PREV";
    case ACT_PLAYPAUSE: return "PLAYPAUSE";
    case ACT_NEXT: return "NEXT";
    case ACT_STOP: return "STOP";
    case ACT_CUSTOM1: return "CUST1";
    case ACT_CUSTOM2: return "CUST2";
    case ACT_CUSTOM3: return "CUST3";
    case ACT_NONE: default: return "NONE";
  }
}

// Forward declarations
void loadConfig();
void saveConfig();
void setDefaults();
void applyLedColors();
void handleSerial();
void sendPots();
void sendLine(const String &s);
void onMainButtonEvent(uint8_t btnIdx, bool down, uint8_t mode);

void setup() {
  Serial.begin(9600);
  // attendre que le terminal série soit prêt (utile pour USB CDC)
  unsigned long start = millis();
  while (millis() - start < 2000) {
    if (Serial) break;
  }
  // initialiser le port UART matériel (Serial1) pour sortie TTL permanente
  Serial1.begin(9600);
  // inputs
  for (int i=0;i<3;i++) {
    pinMode(MAIN_BTN_PINS[i], INPUT_PULLUP);
    lastBtnStateMain[i] = digitalRead(MAIN_BTN_PINS[i]) == LOW; // pressed is LOW
  }
  pinMode(LEFT_BTN_PIN, INPUT_PULLUP);
  pinMode(RIGHT_BTN_PIN, INPUT_PULLUP);
  lastBtnStateLeft = digitalRead(LEFT_BTN_PIN) == LOW;
  lastBtnStateRight = digitalRead(RIGHT_BTN_PIN) == LOW;

  // load config
  loadConfig();

  // indicator LEDs if defined (respect polarity)
  if (cfg.indLeftPin != 255) {
    pinMode(cfg.indLeftPin, OUTPUT);
    // set indicator according to current button state (pressed -> lit)
    bool litLeft = lastBtnStateLeft; // true if pressed (LOW)
  digitalWrite(cfg.indLeftPin, cfg.indLeftActiveHigh ? (litLeft ? LOW : HIGH) : (litLeft ? HIGH : LOW));
  }
  if (cfg.indRightPin != 255) {
    pinMode(cfg.indRightPin, OUTPUT);
    bool litRight = lastBtnStateRight;
  digitalWrite(cfg.indRightPin, cfg.indRightActiveHigh ? (litRight ? LOW : HIGH) : (litRight ? HIGH : LOW));
  }

  // setup NeoPixel strip with configured pin
  if (strip) { delete strip; strip = nullptr; }
  strip = new Adafruit_NeoPixel(NUM_LEDS, cfg.ledPin, NEO_GRB + NEO_KHZ800);
  strip->begin();
  strip->show();
  applyLedColors();

  sendLine("OK:Device Ready");
  sendLine("HELP:Send 'HELP' for commands");
}

void loop() {
  unsigned long now = millis();
  // handle serial commands
  handleSerial();

  // pots periodic send
  if (now - lastPotSend >= POT_SEND_INTERVAL) {
    lastPotSend = now;
    sendPots();
  }

  // read left/right pushbuttons (modifiers)
  bool curLeft = digitalRead(LEFT_BTN_PIN) == LOW;
  bool curRight = digitalRead(RIGHT_BTN_PIN) == LOW;
  if (curLeft != lastBtnStateLeft) {
    lastBtnStateLeft = curLeft;
    // update indicator LED: button pressed => LOW, so lit when pressed
    if (cfg.indLeftPin != 255) {
      bool lit = curLeft; // true if pressed (LOW)
      // if activeHigh==1 then lit -> HIGH else lit -> LOW
  digitalWrite(cfg.indLeftPin, cfg.indLeftActiveHigh ? (lit ? LOW : HIGH) : (lit ? HIGH : LOW));
    }
    // left press may change mode; we don't send separate events for modifiers here
  }
  if (curRight != lastBtnStateRight) {
    lastBtnStateRight = curRight;
    if (cfg.indRightPin != 255) {
      bool lit = curRight;
  digitalWrite(cfg.indRightPin, cfg.indRightActiveHigh ? (lit ? LOW : HIGH) : (lit ? HIGH : LOW));
    }
  }

  // read main buttons with debounce
  for (uint8_t i=0;i<3;i++) {
    bool raw = digitalRead(MAIN_BTN_PINS[i]) == LOW;
    if (raw != lastBtnStateMain[i]) {
      // start debounce timer
      if (lastDebounceTime[i]==0) lastDebounceTime[i] = now;
      else if (now - lastDebounceTime[i] >= DEBOUNCE_MS) {
        // stable
        lastBtnStateMain[i] = raw;
        lastDebounceTime[i] = 0;
        // compute mode
        uint8_t mode = 0;
        if (lastBtnStateLeft && !lastBtnStateRight) mode = 1;
        if (lastBtnStateLeft && lastBtnStateRight) mode = 2;
        onMainButtonEvent(i, raw, mode);
      }
    } else {
      lastDebounceTime[i] = 0;
    }
  }

  // small delay to avoid busy loop
  delay(2);
}

void onMainButtonEvent(uint8_t btnIdx, bool down, uint8_t mode) {
  uint8_t action = cfg.maps[mode][btnIdx];
  String s = "EVT:";
  s += String(mode);
  s += "|";
  s += String(btnIdx+1);
  s += "|";
  s += String((int)action);
  s += "|";
  s += (down?"DOWN":"UP");
  sendLine(s);
  // optionally toggle led on the strip for immediate feedback
  if (action == ACT_PREV && down && strip) {
    int phys = (NUM_LEDS - 1) - 0; // logical 0 -> physical
    strip->setPixelColor(phys, strip->Color(0, 255, 0));
    strip->show();
  }
}

void sendPots() {
  if (cfg.potsEnabled == 0) return;
  uint16_t v[3];
  for (int i=0;i<3;i++) {
    v[i] = analogRead(POT_PINS[i]);
  }
  // format: POTS:v1|v2|v3|20
  String s = "POTS:" + String(v[0]) + "|" + String(v[1]) + "|" + String(v[2]) + "|20";
  sendLine(s);
}

void sendLine(const String &s) {
  // envoyer sur USB CDC et sur le UART matériel (Serial1) afin d'assurer
  // que les messages sont disponibles même sans ouverture du Moniteur Série USB
  Serial.println(s);
  Serial1.println(s);
}

void applyLedColors() {
  if (!strip) return;
  // écrire en inversant l'ordre pour corriger la disposition gauche/droite
  for (int i=0;i<NUM_LEDS;i++) {
    // write logical i to physical index
    int phys = (NUM_LEDS - 1) - i;
    strip->setPixelColor(phys, strip->Color(cfg.colors[i][0], cfg.colors[i][1], cfg.colors[i][2]));
  }
  strip->show();
}

void loadConfig() {
  EEPROM.get(EEPROM_ADDR, cfg);
  if (cfg.magic != CFG_MAGIC || cfg.version != CFG_VERSION) {
    setDefaults();
    saveConfig();
  }
}

void saveConfig() {
  cfg.magic = CFG_MAGIC;
  cfg.version = CFG_VERSION;
  EEPROM.put(EEPROM_ADDR, cfg);
  sendLine("OK:Config saved");
}

void setDefaults() {
  cfg.magic = CFG_MAGIC;
  cfg.version = CFG_VERSION;
  cfg.ledPin = 16;
  cfg.indLeftPin = 7;
  cfg.indLeftActiveHigh = 1;
  cfg.indRightPin = 8;
  cfg.indRightActiveHigh = 1;
  cfg.potsEnabled = 1;
  // default maps: mode0 -> PREV, PLAYPAUSE, NEXT
  cfg.maps[0][0] = ACT_PREV;
  cfg.maps[0][1] = ACT_PLAYPAUSE;
  cfg.maps[0][2] = ACT_NEXT;
  // other modes default same
  for (int m=1;m<3;m++) for (int b=0;b<3;b++) cfg.maps[m][b] = cfg.maps[0][b];
  // default colors: green, white, red
  cfg.colors[0][0] = 0;   cfg.colors[0][1] = 255; cfg.colors[0][2] = 0;   // green
  cfg.colors[1][0] = 255; cfg.colors[1][1] = 255; cfg.colors[1][2] = 255; // white
  cfg.colors[2][0] = 255; cfg.colors[2][1] = 0;   cfg.colors[2][2] = 0;   // red
}

// ---- Serial command parsing ----
String serialBuf = "";

void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuf.length() > 0) {
        String line = serialBuf;
        serialBuf = "";
        // trim
        line.trim();
        if (line.length() > 0) {
          // process
          // simple tokenization
          // split by spaces
          int idx = line.indexOf(' ');
          String cmd = (idx==-1) ? line : line.substring(0, idx);
          String rest = (idx==-1) ? "" : line.substring(idx+1);
          cmd.toUpperCase();
          if (cmd == "HELP") {
            sendLine("CMDS: SET_MAP mode btn action | SET_LED i R G B | SET_ALL R G B | SET_LED_PIN n | SET_IND_PINS L R | GET_MAP | SAVE | RESET | PING");
          } else if (cmd == "PING") {
            sendLine("PONG");
          } else if (cmd == "SET_MAP") {
            // rest: mode btn action
            int m,b,a;
            if (sscanf(rest.c_str(), "%d %d %d", &m,&b,&a) == 3) {
              if (m>=0 && m<3 && b>=1 && b<=3) {
                cfg.maps[m][b-1] = (uint8_t)a;
                sendLine("OK:MAP_SET");
              } else sendLine("ERR:params");
            } else {
              // try name for action
              int m2,b2; char actname[32];
              if (sscanf(rest.c_str(), "%d %d %31s", &m2,&b2, actname) == 3) {
                String sact = String(actname);
                sact.toUpperCase();
                uint8_t code = ACT_NONE;
                if (sact == "PREV") code = ACT_PREV;
                else if (sact == "PLAY" || sact == "PLAYPAUSE") code = ACT_PLAYPAUSE;
                else if (sact == "NEXT") code = ACT_NEXT;
                else if (sact == "STOP") code = ACT_STOP;
                else if (sact == "CUST1") code = ACT_CUSTOM1;
                else if (sact == "CUST2") code = ACT_CUSTOM2;
                else if (sact == "CUST3") code = ACT_CUSTOM3;
                if (m2>=0 && m2<3 && b2>=1 && b2<=3) {
                  cfg.maps[m2][b2-1] = code;
                  sendLine("OK:MAP_SET_NAME");
                } else sendLine("ERR:params");
              } else sendLine("ERR:SET_MAP_SYNTAX");
            }
          } else if (cmd == "GET_MAP") {
            String s = "MAP:";
            for (int m=0;m<3;m++) {
              s += "M" + String(m) + "{";
              for (int b=0;b<3;b++) {
                s += String((int)cfg.maps[m][b]);
                if (b<2) s += ",";
              }
              s += "}";
              if (m<2) s += ";";
            }
            sendLine(s);
          } else if (cmd == "SET_LED") {
            // SET_LED i R G B
            int i,r,g,b;
            if (sscanf(rest.c_str(), "%d %d %d %d", &i,&r,&g,&b) == 4) {
              if (i>=1 && i<=NUM_LEDS) {
                cfg.colors[i-1][0] = (uint8_t)r;
                cfg.colors[i-1][1] = (uint8_t)g;
                cfg.colors[i-1][2] = (uint8_t)b;
                applyLedColors();
                sendLine("OK:LED_SET");
              } else sendLine("ERR:LED_INDEX");
            } else sendLine("ERR:SET_LED_SYNTAX");
          } else if (cmd == "SET_ALL") {
            int r,g,b;
            if (sscanf(rest.c_str(), "%d %d %d", &r,&g,&b) == 3) {
              for (int i=0;i<NUM_LEDS;i++) {
                cfg.colors[i][0] = (uint8_t)r;
                cfg.colors[i][1] = (uint8_t)g;
                cfg.colors[i][2] = (uint8_t)b;
              }
              applyLedColors();
              sendLine("OK:ALL_SET");
            } else sendLine("ERR:SET_ALL_SYNTAX");
          } else if (cmd == "SET_LED_PIN") {
            int p;
            if (sscanf(rest.c_str(), "%d", &p) == 1) {
              if (p >= 0 && p <= 128) {
                cfg.ledPin = (uint8_t)p;
                // reinit NeoPixel strip
                if (strip) { delete strip; strip = nullptr; }
                strip = new Adafruit_NeoPixel(NUM_LEDS, cfg.ledPin, NEO_GRB + NEO_KHZ800);
                strip->begin();
                strip->show();
                applyLedColors();
                sendLine("OK:LED_PIN_SET");
              } else sendLine("ERR:PIN_RANGE");
            } else sendLine("ERR:SET_LED_PIN_SYNTAX");
          } else if (cmd == "SET_IND_PINS") {
            int l,r;
            if (sscanf(rest.c_str(), "%d %d", &l,&r) == 2) {
              cfg.indLeftPin = (uint8_t)l;
              cfg.indRightPin = (uint8_t)r;
              if (cfg.indLeftPin != 255) pinMode(cfg.indLeftPin, OUTPUT);
              if (cfg.indRightPin != 255) pinMode(cfg.indRightPin, OUTPUT);
              sendLine("OK:IND_SET");
            } else sendLine("ERR:SET_IND_SYNTAX");
          } else if (cmd == "SET_IND_POL") {
            int lp,rp;
            if (sscanf(rest.c_str(), "%d %d", &lp,&rp) == 2) {
              cfg.indLeftActiveHigh = (uint8_t)(lp?1:0);
              cfg.indRightActiveHigh = (uint8_t)(rp?1:0);
              sendLine("OK:IND_POL_SET");
            } else sendLine("ERR:SET_IND_POL_SYNTAX");
          } else if (cmd == "POTS_ON") {
            cfg.potsEnabled = 1;
            sendLine("OK:POTS_ON");
          } else if (cmd == "POTS_OFF") {
            cfg.potsEnabled = 0;
            sendLine("OK:POTS_OFF");
          } else if (cmd == "SAVE") {
            saveConfig();
          } else if (cmd == "RESET") {
            setDefaults();
            saveConfig();
          } else if (cmd == "GET_CFG") {
            String s = "CFG:LED_PIN=" + String((int)cfg.ledPin) + ",INDL=" + String((int)cfg.indLeftPin) + ",INDR=" + String((int)cfg.indRightPin)
              + ",INDL_POL=" + String((int)cfg.indLeftActiveHigh) + ",INDR_POL=" + String((int)cfg.indRightActiveHigh)
              + ",POTS=" + String((int)cfg.potsEnabled);
            sendLine(s);
          } else {
            sendLine("ERR:UnknownCmd");
          }
        }
      }
    } else {
      serialBuf += c;
      // limit buffer
      if (serialBuf.length() > 200) serialBuf = serialBuf.substring(serialBuf.length()-200);
    }
  }
}
