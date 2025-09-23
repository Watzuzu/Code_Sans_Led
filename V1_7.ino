#include <HID-Project.h>
#include <FastLED.h>
#include <EEPROM.h>

// Define FAST_INPUT_ONLY = 1 to disable LEDs/slider work and prioritize input/HID paths
#ifndef FAST_INPUT_ONLY
#define FAST_INPUT_ONLY 1
#endif

// Déclaration des broches pour les boutons
const int boutonPlayPause = 2;
const int boutonPrecedent = 3;
const int boutonSuivant = 4;
const int poussBoutG = 5;
const int poussBoutD = 6;
const int LedG = 7;
const int LedD = 8;
const int LED_PIN = 16;
const int NUM_LEDS = 3;

CRGB leds[NUM_LEDS];
byte bright = 50; // luminosité des LEDs
byte baza = 0;
int speed = 20;
int color1 = 255;
int color2 = 255;
int color3 = 255;
bool rambow = false;
bool cyclone = false;

// Déclaration des broches pour les potentiomètres
const int NUM_SLIDERS = 3; // Utiliser seulement 3 potentiomètres
const int analogInputs[NUM_SLIDERS] = {A0, A1, A2}; // A0, A1, A2 pour les potentiomètres

int analogSliderValues[NUM_SLIDERS];

// Button mapping persisted in EEPROM
// Index 0 = boutonPlayPause, 1 = boutonPrecedent, 2 = boutonSuivant
enum ButtonAction : uint8_t { ACT_NONE = 0, ACT_PLAYPAUSE = 1, ACT_PREVIOUS = 2, ACT_NEXT = 3 };
uint8_t buttonMapping[3] = { ACT_PLAYPAUSE, ACT_PREVIOUS, ACT_NEXT }; // defaults
const int EEPROM_MAP_ADDR = 0; // starting address in EEPROM to store 3 bytes

// Serial parsing buffer
String serialBuffer = "";

// Debounce / edge detection for instant activation
const int BUTTON_COUNT = 3; // playpause, previous, next
const int buttonPins[BUTTON_COUNT] = { boutonPlayPause, boutonPrecedent, boutonSuivant };
int lastButtonState[BUTTON_COUNT]; // HIGH or LOW
unsigned long lastDebounceTime[BUTTON_COUNT];
const unsigned long DEBOUNCE_DELAY = 20; // ms software debounce to avoid multiple triggers
const unsigned long IRQ_DEBOUNCE_MS = 30; // ms minimal spacing between IRQ-handled triggers
int stableButtonState[BUTTON_COUNT];
// Animation / effect timers to avoid blocking
unsigned long lastAnimMillis = 0;
unsigned long lastEffectToggleMillis = 0;
const unsigned long EFFECT_TOGGLE_DEBOUNCE = 10; // ms
bool ledsDirty = false;
// IRQ-based immediate detection
volatile bool irqButtonPressed[BUTTON_COUNT] = { false, false, false };
unsigned long lastIrqHandledTime[BUTTON_COUNT] = {0,0,0};
bool irqAttached[BUTTON_COUNT] = { false, false, false };

// ISR prototypes
void isr_button0();
void isr_button1();
void isr_button2();

void setup() {
  // Initialisation des broches comme entrées avec résistance de tirage interne
  pinMode(boutonPlayPause, INPUT_PULLUP);
  pinMode(boutonPrecedent, INPUT_PULLUP);
  pinMode(boutonSuivant, INPUT_PULLUP);
  pinMode(poussBoutG, INPUT_PULLUP);
  pinMode(poussBoutD, INPUT_PULLUP);
  pinMode(LedG, OUTPUT);
  pinMode(LedD, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

#if !FAST_INPUT_ONLY
  FastLED.addLeds <WS2812, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  leds[0] = CRGB::Red;
  leds[1] = CRGB::White;
  leds[2] = CRGB::Blue;
#endif

    // Initialisation des broches pour les potentiomètres
#if !FAST_INPUT_ONLY
  for (int i = 0; i < NUM_SLIDERS; i++) {
    pinMode(analogInputs[i], INPUT);
  }
#endif

  digitalWrite(LedD, HIGH);
  digitalWrite(LedG, HIGH);

  delay(500);

  digitalWrite(LedD, LOW);
  digitalWrite(LedG, LOW);

  delay(500);

  digitalWrite(LedD, HIGH);
  digitalWrite(LedG, HIGH);

  delay(500);

  digitalWrite(LedD, LOW);
  digitalWrite(LedG, LOW);

  delay(500);

  digitalWrite(LedD, HIGH);
  digitalWrite(LedG, HIGH);

  delay(500);

  digitalWrite(LedD, LOW);
  digitalWrite(LedG, LOW);

  // Initialiser HID (clavier)
  Consumer.begin();
  Serial.begin(9600);
  loadMappingFromEEPROM();

  // init debounce states
  for (int i = 0; i < BUTTON_COUNT; i++) {
    lastButtonState[i] = digitalRead(buttonPins[i]);
    lastDebounceTime[i] = 0;
    stableButtonState[i] = lastButtonState[i];
  }

  // Try to attach interrupts for minimal latency (if platform supports it)
  // digitalPinToInterrupt(pin) returns -1 if not attachable on some pins/boards
  int irq0 = digitalPinToInterrupt(buttonPins[0]);
  int irq1 = digitalPinToInterrupt(buttonPins[1]);
  int irq2 = digitalPinToInterrupt(buttonPins[2]);
  if (irq0 != NOT_AN_INTERRUPT) { attachInterrupt(irq0, isr_button0, FALLING); irqAttached[0] = true; }
  if (irq1 != NOT_AN_INTERRUPT) { attachInterrupt(irq1, isr_button1, FALLING); irqAttached[1] = true; }
  if (irq2 != NOT_AN_INTERRUPT) { attachInterrupt(irq2, isr_button2, FALLING); irqAttached[2] = true; }

}

void loop() {
  // Process IRQ flags first for absolute minimal latency
  unsigned long now_irq = millis();
  for (int i = 0; i < BUTTON_COUNT; i++) {
    if (irqButtonPressed[i]) {
      // clear flag immediately to avoid ISR retriggering race while we process
      irqButtonPressed[i] = false;
      // coarse debounce for IRQ path to avoid multiple firings from mechanical bounce
      if (now_irq - lastIrqHandledTime[i] > IRQ_DEBOUNCE_MS) {
        lastIrqHandledTime[i] = now_irq;
        // Use immediate press/release mapped to proper HID code
        performButtonActionImmediate(buttonMapping[i]);
        // mark software state as pressed so polling doesn't re-trigger the same press
        lastButtonState[i] = LOW;
        stableButtonState[i] = LOW;
        lastDebounceTime[i] = now_irq;
      }
    }
  }

  // Parse serial commands (non bloquant)
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (serialBuffer.length() > 0) {
        processSerialCommand(serialBuffer);
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
      // limit buffer size
      if (serialBuffer.length() > 128) serialBuffer = serialBuffer.substring(serialBuffer.length() - 128);
    }
  }

  

  // Handle button edge detection only if IRQs not available
  if (!(irqAttached[0] && irqAttached[1] && irqAttached[2])) {
    readButtonsEdge();
  }
  // get current time for animations and effects
  unsigned long now = millis();

#if !FAST_INPUT_ONLY
  // Update animations non-blocking; mark LEDs dirty instead of showing immediately
  FastLED.setBrightness(bright);
  if (rambow == true) {
    if (now - lastAnimMillis >= max(1, speed)) {
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CHSV(baza + i * 5, 255, 255);
      }
      baza++;
      lastAnimMillis = now;
      ledsDirty = true;
    }
  }
#endif

  // Lire l'état des boutons (LOW = appuyé, HIGH = relâché)
  if(digitalRead(poussBoutG) == HIGH) {
    // non-blocking toggles on other buttons when poussBoutG is HIGH
    // use EFFECT_TOGGLE_DEBOUNCE to avoid multiple toggles
    unsigned long nowEffect = millis();
    if (digitalRead(boutonPrecedent) == LOW && (nowEffect - lastEffectToggleMillis >= EFFECT_TOGGLE_DEBOUNCE)) {
      cyclone = false;
      rambow = !rambow;
      lastEffectToggleMillis = nowEffect;
    }

    if (digitalRead(boutonPlayPause) == LOW && (nowEffect - lastEffectToggleMillis >= EFFECT_TOGGLE_DEBOUNCE)) {
      rambow = false;
      cyclone = !cyclone;
      lastEffectToggleMillis = nowEffect;
    }

    if (digitalRead(boutonSuivant) == LOW && (nowEffect - lastEffectToggleMillis >= EFFECT_TOGGLE_DEBOUNCE)) {
      rambow = false;
      cyclone = false;
#if !FAST_INPUT_ONLY
  leds[0] = CRGB::Red;
  leds[1] = CRGB::White;
  leds[2] = CRGB::Blue;
  ledsDirty = true;
#endif
      lastEffectToggleMillis = nowEffect;
    }

  } else {
    // Use edge-detection debounce to trigger actions instantly on press
    // (already called earlier as priority)
  }

  // cyclone effect set colors (non-blocking, colors update immediately)
#if !FAST_INPUT_ONLY
  if (cyclone == true) {
    leds[0] = CRGB(color1, color2, color3);
    leds[1] = CRGB(color1, color2, color3);
    leds[2] = CRGB(color1, color2, color3);
    ledsDirty = true;
  }
#endif


  if (digitalRead(poussBoutG) == LOW) {
    digitalWrite(LedG, LOW); // Envoi la commande suivant
  }else{
    digitalWrite(LedG, HIGH);
  }

  if (digitalRead(poussBoutD) == LOW) {
    digitalWrite(LedD, LOW); // Envoi la commande suivant
  }else{
    digitalWrite(LedD, HIGH);
  }

#if !FAST_INPUT_ONLY
  if (digitalRead(poussBoutG) == LOW) {
    updateSliderValues(); // Lire les valeurs des potentiomètres
    sendSliderValues();   // Envoyer les valeurs sur le port série
  }else{
    if (digitalRead(poussBoutD) == LOW) {
      bright = (analogRead(analogInputs[0]) / 4);
      speed = (analogRead(analogInputs[1]) / 25);
    }else {
      color1 = (analogRead(analogInputs[0]) / 4);
      color2 = (analogRead(analogInputs[1]) / 4);
      color3 = (analogRead(analogInputs[2]) / 4);
    }

  }
#endif

  // no blocking delay to keep input latency minimal
  // show LEDs once per loop if needed
  if (ledsDirty) {
    FastLED.show();
    ledsDirty = false;
  }
}

// Effectue l'action associée au bouton
void performButtonAction(uint8_t action) {
  switch (action) {
    case ACT_PLAYPAUSE:
      Consumer.write(MEDIA_PLAY_PAUSE);
      break;
    case ACT_PREVIOUS:
      Consumer.write(MEDIA_PREVIOUS);
      break;
    case ACT_NEXT:
      Consumer.write(MEDIA_NEXT);
      break;
    default:
      // ACT_NONE ou non reconnu -> ne rien faire
      break;
  }
}

// EEPROM helpers
void loadMappingFromEEPROM() {
  for (int i = 0; i < 3; i++) {
    uint8_t v = EEPROM.read(EEPROM_MAP_ADDR + i);
    if (v >= ACT_NONE && v <= ACT_NEXT) {
      buttonMapping[i] = v;
    } else {
      // leave default if invalid
    }
  }
}

void saveMappingToEEPROM() {
  for (int i = 0; i < 3; i++) {
    EEPROM.update(EEPROM_MAP_ADDR + i, buttonMapping[i]);
  }
}

// Serial command processor
// Commands:
// SET <idx> <ACTION>  e.g. "SET 0 PLAYPAUSE" or "SET 2 NEXT"
// GET                -> returns current mapping
// RESET              -> restore defaults
void processSerialCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.startsWith("SET ")) {
    // parse
    int firstSpace = cmd.indexOf(' ');
    int secondSpace = cmd.indexOf(' ', firstSpace + 1);
    if (secondSpace > 0) {
      String idxStr = cmd.substring(firstSpace + 1, secondSpace);
      String actStr = cmd.substring(secondSpace + 1);
      int idx = idxStr.toInt();
      uint8_t act = ACT_NONE;
      if (actStr == "PLAYPAUSE") act = ACT_PLAYPAUSE;
      else if (actStr == "PREVIOUS") act = ACT_PREVIOUS;
      else if (actStr == "NEXT") act = ACT_NEXT;

      if (idx >= 0 && idx < 3) {
        buttonMapping[idx] = act;
        saveMappingToEEPROM();
        // Reset debounce/stable states so mapping takes effect immediately
        unsigned long now = millis();
        for (int j = 0; j < BUTTON_COUNT; j++) {
          lastDebounceTime[j] = now;
          lastButtonState[j] = digitalRead(buttonPins[j]);
          stableButtonState[j] = lastButtonState[j];
        }
        // Clear IRQ flags and timers to avoid double/triple triggers
        for (int j = 0; j < BUTTON_COUNT; j++) {
          irqButtonPressed[j] = false;
          lastIrqHandledTime[j] = now;
        }
        // If any button is currently pressed, trigger its action immediately
        for (int j = 0; j < BUTTON_COUNT; j++) {
          if (digitalRead(buttonPins[j]) == LOW) {
            performButtonAction(buttonMapping[j]);
          }
        }
        Serial.println("OK");
        return;
      }
    }
    Serial.println("ERR");
    return;
  }

  if (cmd == "GET") {
    // print mapping as e.g. "0:PLAYPAUSE,1:PREVIOUS,2:NEXT"
    String out = "";
    for (int i = 0; i < 3; i++) {
      if (i) out += ",";
      out += String(i) + ":";
      switch (buttonMapping[i]) {
        case ACT_PLAYPAUSE: out += "PLAYPAUSE"; break;
        case ACT_PREVIOUS: out += "PREVIOUS"; break;
        case ACT_NEXT: out += "NEXT"; break;
        default: out += "NONE"; break;
      }
    }
    Serial.println(out);
    return;
  }

  if (cmd == "RESET") {
    buttonMapping[0] = ACT_PLAYPAUSE;
    buttonMapping[1] = ACT_PREVIOUS;
    buttonMapping[2] = ACT_NEXT;
    saveMappingToEEPROM();
    // Reset debounce/stable states so mapping takes effect immediately
    unsigned long now = millis();
    for (int j = 0; j < BUTTON_COUNT; j++) {
      lastDebounceTime[j] = now;
      lastButtonState[j] = digitalRead(buttonPins[j]);
      stableButtonState[j] = lastButtonState[j];
    }
    // If any button is currently pressed, trigger its action immediately
    for (int j = 0; j < BUTTON_COUNT; j++) {
      if (digitalRead(buttonPins[j]) == LOW) {
        performButtonAction(buttonMapping[j]);
      }
    }
    Serial.println("OK");
    return;
  }

  Serial.println("UNKNOWN");
}

// Non-blocking edge detection for button presses
void readButtonsEdge() {
  unsigned long now = millis();
  for (int i = 0; i < BUTTON_COUNT; i++) {
    // skip polling for pins if IRQ attached; IRQ path handles detection
    if (irqAttached[i]) continue;
    int reading = digitalRead(buttonPins[i]);
    if (reading != lastButtonState[i]) {
      // change detected -> immediate falling edge trigger
      lastDebounceTime[i] = now;
      // immediate activation on HIGH->LOW
      if (lastButtonState[i] == HIGH && reading == LOW) {
        performButtonAction(buttonMapping[i]);
        // consider it stable pressed to avoid retrigger from bounce
        stableButtonState[i] = LOW;
        lastButtonState[i] = reading;
        // set debounce base time
        lastDebounceTime[i] = now;
        continue; // go to next button
      }
    }

    // regular debounce/stable update
    if ((now - lastDebounceTime[i]) > DEBOUNCE_DELAY) {
      if (reading != stableButtonState[i]) {
        stableButtonState[i] = reading;
        // falling edge after stable period (backup)
        if (stableButtonState[i] == LOW) {
          performButtonAction(buttonMapping[i]);
        }
      }
    }

    lastButtonState[i] = reading;
  }
}

// ISR implementations: set flag only (keep ISR short)
void isr_button0() {
  irqButtonPressed[0] = true;
}
void isr_button1() {
  irqButtonPressed[1] = true;
}
void isr_button2() {
  irqButtonPressed[2] = true;
}

// Immediate action using press/release for lower HID latency
void performButtonActionImmediate(uint8_t action) {
  switch (action) {
    case ACT_PLAYPAUSE:
  Consumer.press(MEDIA_PLAY_PAUSE);
  delayMicroseconds(50);
  Consumer.release(MEDIA_PLAY_PAUSE);
      break;
    case ACT_PREVIOUS:
  Consumer.press(MEDIA_PREVIOUS);
  delayMicroseconds(50);
  Consumer.release(MEDIA_PREVIOUS);
      break;
    case ACT_NEXT:
  Consumer.press(MEDIA_NEXT);
  delayMicroseconds(50);
  Consumer.release(MEDIA_NEXT);
      break;
    default:
      break;
  }
}

void updateSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
     analogSliderValues[i] = analogRead(analogInputs[i]);
  }
}

void sendSliderValues() {
  String builtString = String("");

  for (int i = 0; i < NUM_SLIDERS; i++) {
    builtString += String((int)analogSliderValues[i]);

    if (i < NUM_SLIDERS - 1) {
      builtString += String("|");
    }
  }

  builtString += String("|20");
  
  Serial.println(builtString); // Envoi des valeurs des potentiomètres au port série
}

void printSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    String printedString = String("Slider #") + String(i + 1) + String(": ") + String(analogSliderValues[i]) + String(" mV");
    Serial.write(printedString.c_str());

    if (i < NUM_SLIDERS - 1) {
      Serial.write(" | ");
    } else {
      Serial.write("\n");
    }
  }
}