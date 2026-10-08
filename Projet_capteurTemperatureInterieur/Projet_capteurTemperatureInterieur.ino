#include "LiquidCrystal_I2C.h"
#include "DHT.h"
#include "SoftwareSerial.h"
#include "Wire.h"

#define DHTPIN 17 // A3
#define DHTTYPE DHT11

#define EMETTEUR 2
#define DESTINATAIRE 0

DHT dht(DHTPIN, DHTTYPE);

SoftwareSerial XBee(2, 3);

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Serial.begin(9600);
  XBee.begin(9600);
  dht.begin();
  lcd.init();
  creerBarres();
  lcd.clear();
  lcd.backlight();
  lcd.display();
  lcd.setCursor(0, 0);
}

void loop() {

  
  attendreAvecBarre(5000);

  // Lecture de la température
  float temperature = dht.readTemperature();

  uint16_t data;

  if (isnan(temperature)) {
    logTime();
    Serial.println("Echec reception");

    // Valeur indiquant une erreur
    data = 0x1FF;
    lcd.setCursor(0, 0);
    lcd.print("Erreur     ");
  }
  else {
    int temperatureInt = (int)temperature;

    logTime();
    Serial.print("Temperature : ");
    Serial.print(temperatureInt);
    Serial.println(" °C");

    lcd.setCursor(0, 0);
    lcd.print(temperatureInt);
    lcd.print((char) 223);
    lcd.print("C     ");

    // Température entière sur 10 bits
    data = temperatureInt;
  }

  // 1. Octet de synchronisation
  uint8_t trame1 = 0xAA;

  // 2. Destinataire (3 bits) + émetteur (3 bits) + 2 bits de DATA
  uint8_t trame2 =
      (DESTINATAIRE << 5) |
      (EMETTEUR << 2) |
      ((data >> 8) & 0x03);

  // 3. 8 bits restants de DATA
  uint8_t trame3 = data & 0xFF;

  // 4. Checksum
  uint8_t checksum =
      (trame1 + trame2 + trame3) % 256;

  // Envoi des 4 octets
  XBee.write(trame1);
  XBee.write(trame2);
  XBee.write(trame3);
  XBee.write(checksum);

  // Affichage de la trame
  logTime();
  Serial.print("Trame envoyee : ");
  Serial.print(trame1, HEX);
  Serial.print(" ");
  Serial.print(trame2, HEX);
  Serial.print(" ");
  Serial.print(trame3, HEX);
  Serial.print(" ");
  Serial.println(checksum, HEX);
}

void logTime() {
  Serial.print("[");
  Serial.print(millis() / 1000);
  Serial.print(" s] ");
}

// Crée les caractères 1 à 5 : 1 colonne remplie, 2 colonnes, ... 5 (bloc plein)
void creerBarres() {
  for (byte i = 1; i <= 5; i++) {
    byte motif[8];
    byte ligne = (0x1F << (5 - i)) & 0x1F;
    for (byte j = 0; j < 8; j++) motif[j] = ligne;
    lcd.createChar(i, motif);
  }
}

// Dessine la barre sur la ligne 2 (pas de 0 à 80)
void dessinerBarre(int pas) {
  lcd.setCursor(0, 1);
  for (int c = 0; c < 16; c++) {
    int reste = pas - c * 5;
    if (reste >= 5)     lcd.write((byte)5);
    else if (reste > 0) lcd.write((byte)reste);
    else                lcd.write(' ');
  }
}

// Remplace delay() : attend "duree" ms en faisant avancer la barre
void attendreAvecBarre(unsigned long duree) {
  const int total = 16 * 5;
  unsigned long debut = millis();
  int dernier = -1;
  while (millis() - debut < duree) {
    int pas = (millis() - debut) * total / duree;
    if (pas != dernier) {
      dessinerBarre(pas);
      dernier = pas;
    }
  }
  dessinerBarre(total);
}
