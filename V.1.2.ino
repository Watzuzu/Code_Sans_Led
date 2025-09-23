#include <HID-Project.h>
#include <FastLED.h>

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

  FastLED.addLeds <WS2812, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  leds[0] = CRGB::Red;
  leds[1] = CRGB::White;
  leds[2] = CRGB::Blue;

    // Initialisation des broches pour les potentiomètres
  for (int i = 0; i < NUM_SLIDERS; i++) {
    pinMode(analogInputs[i], INPUT);
  }

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

}

void loop() {

  FastLED.setBrightness(bright);
  FastLED.show();

  // Lire l'état des boutons (LOW = appuyé, HIGH = relâché)
  if(digitalRead(poussBoutG) == HIGH) {
    if(digitalRead(boutonPrecedent) == LOW){
      if(cyclone == true){
        cyclone = false;
      }
      if(rambow == false) {
        rambow = true;
        delay(200);
      } else {
        rambow = false;
        delay(200);
      }
    }

    if(digitalRead(boutonPlayPause) == LOW){
      if(rambow == true){
        rambow = false;
      }
      if(cyclone == false) {
        cyclone = true;
        delay(200);
      } else {
        cyclone = false;
        delay(200);
      }
    }

    if(digitalRead(boutonSuivant) == LOW){
      if(rambow == true){
        rambow = false;
      }

      if(cyclone == true){
        cyclone = false;

      }
      leds[0] = CRGB::Red;
      leds[1] = CRGB::White;
      leds[2] = CRGB::Blue;
    }

  }else {

    if (digitalRead(boutonPlayPause) == LOW) {
      delay(20); // Antirebond
      if (digitalRead(boutonPlayPause) == LOW) {
        Consumer.write(MEDIA_PLAY_PAUSE); // Envoi la commande play/pause
        while (digitalRead(boutonPlayPause) == LOW); // Attendre que le bouton soit relâché
      }
    }

    if (digitalRead(boutonPrecedent) == LOW) {
      delay(20); // Antirebond
      if (digitalRead(boutonPrecedent) == LOW) {
        Consumer.write(MEDIA_PREVIOUS); // Envoi la commande précédent
        while (digitalRead(boutonPrecedent) == LOW); // Attendre que le bouton soit relâché
      }
    }

    if (digitalRead(boutonSuivant) == LOW) {
      delay(20); // Antirebond
      if (digitalRead(boutonSuivant) == LOW) {
        Consumer.write(MEDIA_NEXT); // Envoi la commande suivant
        while (digitalRead(boutonSuivant) == LOW); // Attendre que le bouton soit relâché
      }
    }

  }

  if (rambow == true) {

    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CHSV(baza+ i * 5, 255, 255);
      }
      baza++;
      delay(speed);

  }

  if (cyclone == true) {
    leds[0] = CRGB(color1, color2, color3);
    leds[1] = CRGB(color1, color2, color3);
    leds[2] = CRGB(color1, color2, color3);
  }


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

  delay(10); // Petit délai pour éviter de saturer le processeur
  
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