/*
 ****************************************************************************
 *                                                                          *
 *              CAPTEUR DE TEMPERATURE INTERIEUR                            *
 *                                                                          *
 *  Ce script mesure la temperature interieure avec un capteur DHT11,       *
 *  l'affiche sur un ecran LCD et transmet la valeur à un relais            *
 *  via un module XBee.                                                     *
 *                                                                          *
 *  Auteurs : Mathys GESLIN et Thomas HYAUMET                               *
 *                                                                          *
 ****************************************************************************
 */

#include "LiquidCrystal_I2C.h"
#include "DHT.h"
#include "SoftwareSerial.h"
#include "Wire.h"

#include <frame_protocol.h>
#include <xbee_at.h>
#include <ecostore_nodes.h>

#define DHTPIN 17 // A3
#define DHTTYPE DHT11

#define LCD_COLONNES 16 // Nombre de colonnes de l'ecran LCD
#define LCD_LIGNES 2 // Nombre de lignes de l'ecran LCD
#define CODE_ERREUR 0xFF // Valeur envoyee lorsque la lecture du capteur echoue.

#define PERIODE_CHRONO_MS 1000 // Rafraichissement du chrono
#define DUREE_RX_MS 300 // Duree d'affichage de RX
#define DUREE_TX_MS 500 // Duree d'affichage de TX

DHT dht(DHTPIN, DHTTYPE);

SoftwareSerial XBee(2, 3);

LiquidCrystal_I2C lcd(0x27, LCD_COLONNES, LCD_LIGNES);

FrameParser_t parseur;
unsigned long derniereDemande = 0;
bool demandeRecue = false;
unsigned long dernierAffichage = 0;

// Indicateur d'activite radio (RX / TX) affiche en fin de ligne 1
unsigned long debutEchange = 0;
bool echangeEnCours = false;
uint8_t etatIndicateur = 0;

void setup() {
  Serial.begin(XBEE_BAUD);
  XBee.begin(XBEE_BAUD);
  frame_parser_init(&parseur);
  dht.begin();
  lcd.init();
  lcd.clear();
  lcd.backlight();
  lcd.display();

  afficherLigne(0, "En attente");
  afficherStatut();
}

void loop() {
  while (XBee.available()) {
    FrameMsg_t messageRecu;

    if (frame_parse_byte(&parseur, (uint8_t)XBee.read(), &messageRecu) &&
        messageRecu.dest_id == NODE_CAPTEUR_TEMPERATURE_INTERNE &&
        messageRecu.cmd == FRAME_CMD_READ) {
      derniereDemande = millis();
      demandeRecue = true;
      debutEchange = derniereDemande;
      echangeEnCours = true;
      calculerIndicateur();
      afficherStatut();
      envoyerTemperature();
    }
  }

  mettreAJourStatut();
}

void envoyerTemperature() {
  // Lecture de la température
  float temperature = dht.readTemperature();

  uint8_t data;
  FrameCmd_t commande;

  if (isnan(temperature)) {
    logTime();
    Serial.println("Echec lecture");

    // Valeur indiquant une erreur
    data = CODE_ERREUR;
    commande = FRAME_CMD_ERROR;

    afficherLigne(0, "Erreur capteur");
  }
  else {
    int temperatureInt = (int)temperature;

    logTime();
    Serial.print("Temperature : ");
    Serial.print(temperatureInt);
    Serial.println(" °C");

    char texte[LCD_COLONNES + 1];
    snprintf(texte, sizeof(texte), "Temp: %d %cC", temperatureInt, (char)223);
    afficherLigne(0, texte);

    commande = FRAME_CMD_WRITE;

    // Température entière sur 8 bits
    data = temperatureInt;
  }

  FrameMsg_t message = {
      .dest_id = NODE_HUB,
      .src_id = NODE_CAPTEUR_TEMPERATURE_INTERNE,
      .cmd = commande,
      .value = data
  };

  uint8_t trame[FRAME_TOTAL_SIZE];
  frame_pack(&message, trame);
  XBee.write(trame, FRAME_TOTAL_SIZE);

  // Affichage de la trame
  logTime();
  Serial.print("Trame envoyee : ");
  for (uint8_t i = 0; i < FRAME_TOTAL_SIZE; i++) {
    Serial.print(trame[i], HEX);
    if (i < FRAME_TOTAL_SIZE - 1) Serial.print(" ");
  }
  Serial.println();
}

// Ecrit un texte sur une ligne, complete a 16 caracteres (pas besoin d'effacer)
void afficherLigne(uint8_t ligne, const char *texte) {
  char buffer[LCD_COLONNES + 1];
  snprintf(buffer, sizeof(buffer), "%-16s", texte);
  lcd.setCursor(0, ligne);
  lcd.print(buffer);
}

// Détermine l'indicateur selon le temps écoulé depuis le début de l'échange
void calculerIndicateur() {
  if (!echangeEnCours) {
    etatIndicateur = 0;
    return;
  }

  unsigned long tempsEcoule = millis() - debutEchange;
  if (tempsEcoule < DUREE_RX_MS) {
    etatIndicateur = 1;
  } else if (tempsEcoule < DUREE_RX_MS + DUREE_TX_MS) {
    etatIndicateur = 2;
  } else {
    etatIndicateur = 0;
    echangeEnCours = false;
  }
}

// Ligne 1 : "Depuis 12 s    RX" (13 colonnes pour le chrono, 3 pour l'indicateur)
void afficherStatut() {
  char gauche[LCD_COLONNES + 1];
  char ligne[LCD_COLONNES + 1];
  char indicateur[4];

  if (demandeRecue) {
    unsigned long secondes = (millis() - derniereDemande) / 1000;
    snprintf(gauche, sizeof(gauche), "Depuis %lu s", secondes);
  } else {
    snprintf(gauche, sizeof(gauche), "Depuis -- s");
  }

  if (etatIndicateur == 1) {
    snprintf(indicateur, sizeof(indicateur), "RX");
  } else if (etatIndicateur == 2) {
    snprintf(indicateur, sizeof(indicateur), "TX");
  } else {
    indicateur[0] = '\0';
  }

  // %-13.13s : complete ou tronque a 13 caracteres, %3s : indicateur aligne a droite
  snprintf(ligne, sizeof(ligne), "%-13.13s%3s", gauche, indicateur);
  lcd.setCursor(0, 1);
  lcd.print(ligne);

  dernierAffichage = millis();
}

// Rafraichit la ligne 1 si l'indicateur change ou si une seconde s'est écoulée
void mettreAJourStatut() {
  unsigned long maintenant = millis();
  uint8_t ancienEtat = etatIndicateur;
  calculerIndicateur();

  if (ancienEtat != etatIndicateur ||
      (demandeRecue && maintenant - dernierAffichage >= PERIODE_CHRONO_MS)) {
    afficherStatut();
  }
}

void logTime() {
  Serial.print("[");
  Serial.print(millis() / 1000);
  Serial.print(" s] ");
}
