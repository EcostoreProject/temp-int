/*
 ****************************************************************************
 *                                                                          *
 *              CAPTEUR DE TEMPERATURE INTÉRIEURE                           *
 *                                                                          *
 *  Ce script mesure la température intérieure avec un capteur DHT11,       *
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
#define CODE_ERREUR 0xFF // Valeur envoyée lorsque la lecture du capteur échoue.

#define PERIODE_CHRONO_MS 1000 // Rafraichissement du chrono
#define DUREE_RX_MS 300 // Durée d'affichage de RX
#define DUREE_TX_MS 500 // Durée d'affichage de TX

DHT dht(DHTPIN, DHTTYPE);

SoftwareSerial XBee(2, 3);

LiquidCrystal_I2C lcd(0x27, LCD_COLONNES, LCD_LIGNES);

FrameParser_t parseur;
unsigned long derniereDemande = 0;
bool demandeRecue = false;
unsigned long dernierAffichage = 0;

// Indicateur d'activité radio (RX / TX) affiché en fin de ligne 1
unsigned long debutEchange = 0;
bool echangeEnCours = false;
uint8_t etatIndicateur = 0;

// ============================================================================
// INITIALISATION DU SYSTÈME
// ============================================================================

/**
 * Initialise les communications, le capteur DHT11 et l'écran LCD.
 */
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

// ============================================================================
// BOUCLE PRINCIPALE
// ============================================================================

/**
 * Reçoit et traite les demandes de lecture de température.
 *
 * La fonction analyse les octets reçus par le module XBee, déclenche une
 * mesure lorsqu'une trame valide est reçue, puis actualise l'indicateur
 * d'activité affiché sur l'écran LCD.
 */
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

// ============================================================================
// MESURE ET TRANSMISSION DE LA TEMPÉRATURE
// ============================================================================

/**
 * Lit la température, l'affiche et l'envoie au noeud central.
 *
 * En cas d'échec de lecture du capteur, la fonction affiche une erreur et
 * transmet la valeur CODE_ERREUR avec une commande d'erreur.
 */
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

// ============================================================================
// AFFICHAGE D'UNE LIGNE LCD
// ============================================================================

/**
 * Affiche un texte sur une ligne de l'écran LCD.
 *
 * Le texte est complété avec des espaces jusqu'à LCD_COLONNES caractères afin
 * d'effacer les éventuels caractères restants de l'affichage précédent.
 *
 * @param ligne Numéro de la ligne LCD à utiliser.
 * @param texte Texte à afficher.
 */
void afficherLigne(uint8_t ligne, const char *texte) {
  char buffer[LCD_COLONNES + 1];
  snprintf(buffer, sizeof(buffer), "%-16s", texte);
  lcd.setCursor(0, ligne);
  lcd.print(buffer);
}

// ============================================================================
// GESTION DE L'INDICATEUR D'ACTIVITÉ RADIO
// ============================================================================

/**
 * Détermine l'état de l'indicateur radio selon la durée de l'échange.
 *
 * L'indicateur signale d'abord la réception (RX), puis la transmission (TX).
 * Il est désactivé lorsque la durée totale de l'échange est dépassée.
 */
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

// ============================================================================
// AFFICHAGE DU STATUT
// ============================================================================

/**
 * Affiche le temps depuis la dernière demande et l'activité radio.
 *
 * La ligne LCD réserve 13 caractères au chronometre et 3 caractères à
 * l'indicateur RX ou TX.
 */
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

  // %-13.13s : complète ou tronque à 13 caracteres, %3s : indicateur aligné à droite
  snprintf(ligne, sizeof(ligne), "%-13.13s%3s", gauche, indicateur);
  lcd.setCursor(0, 1);
  lcd.print(ligne);

  dernierAffichage = millis();
}

// ============================================================================
// ACTUALISATION PÉRIODIQUE DU STATUT
// ============================================================================

/**
 * Actualise l'indicateur et le chronomètre de l'écran LCD.
 *
 * L'affichage n'est rafraichi que lorsque l'état RX/TX change ou qu'une
 * seconde s'est écoulée depuis le dernier affichage.
 */
void mettreAJourStatut() {
  unsigned long maintenant = millis();
  uint8_t ancienEtat = etatIndicateur;
  calculerIndicateur();

  if (ancienEtat != etatIndicateur ||
      (demandeRecue && maintenant - dernierAffichage >= PERIODE_CHRONO_MS)) {
    afficherStatut();
  }
}

// ============================================================================
// JOURNALISATION DU TEMPS
// ============================================================================

/**
 * Écrit dans le moniteur série le temps ecoulé depuis le démarrage.
 *
 * Le préfixe produit par cette fonction est formaté sous la forme
 * "[temps en secondes s] ".
 */
void logTime() {
  Serial.print("[");
  Serial.print(millis() / 1000);
  Serial.print(" s] ");
}
