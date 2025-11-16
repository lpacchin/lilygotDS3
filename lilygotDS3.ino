/*
  LILYGO T-Display-S3 — Clock + MQTT Ticker FULLSCREEN (singolo + repeat ogni 30s)
  - Ticker in bigFont a schermo intero, singolo passaggio
  - Se non arrivano nuovi messaggi, ripete l’ultimo ogni 30 secondi
  - Messaggi lunghi: scroll completo, poi 30s di pausa, poi di nuovo
  - Sprite anti-flicker, 4 step luminosità su GPIO0, NTP Europe/Zurich, MQTT

  Librerie:
    - TFT_eSPI (User_Setup per T-Display S3 / Setup206)
    - PubSubClient
*/

// ====== IMPORT / DEFINES IMPORTANTI ======
#define MQTT_MAX_PACKET_SIZE 2048   // <<< permette payload MQTT più lunghi

#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <TFT_eSPI.h>

// ==== FONT LOCALI ====
#include "bigFont.h"
#include "font18.h"         // per la data
#include "midleFont.h"
#include "smallFont.h"
#include "tinyFont.h"

// ==== WiFi / MQTT ====
const char* WIFI_SSID    = "lupa";
const char* WIFI_PASS    = "780130bmw.";

const char* MQTT_HOST    = "10.10.1.248";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER    = "lupa";     // opzionale
const char* MQTT_PASSWD  = "2236";     // opzionale
const char* SUB_TOPIC    = "t-display/notify";

const char* TZ_EU_ZURICH = "CET-1CEST,M3.5.0/02:00:00,M10.5.0/03:00:00";

// ==== Display / colori ====
TFT_eSPI tft;
TFT_eSprite clockSpr(&tft), tickerSpr(&tft);  // ticker FULLSCREEN

#define COL_BG       TFT_BLACK
#define COL_CLOCK    TFT_WHITE
#define COL_DATE     0x7BEF
#define COL_TICKERBG TFT_BLACK
#define COL_TICKER   TFT_RED

const int LCD_W = 320, LCD_H = 170;

// ==== Pin T-Display S3 ====
#define PIN_PWR   15
#define PIN_BL    38
#define PIN_BTN   0

// ==== Backlight (4 step) ====
#define BL_FREQ   10000
#define BL_BITS   8
const uint8_t BL_STEPS[4] = { 32, 96, 160, 255 };
uint8_t blIndex = 0;

// Debounce bottone
bool btnLast = true; // pull-up: HIGH = rilasciato
unsigned long btnLastChange = 0;
const unsigned long BTN_DEBOUNCE_MS = 120;

// ==== Ticker FULLSCREEN (singolo + repeat) ====
const int  TICKER_Y_OFFSET      = -50;      // alza di 20 px (negativo = su)
int        TICKER_SPEED         = 5;        // px per frame
const uint16_t FRAME_MS         = 16;       // ~60 fps
const int EDGE_MARGIN           = 4;        // margine per uscita completa a sinistra
const unsigned long TICKER_REPEAT_MS = 60000000; // 30 secondi tra una ripetizione e l’altra

String tickerMsg;
String lastTickerMsg;                 // ultimo messaggio valido (pulito)
int tickerX   = 0;                    // posizione corrente del testo
int textW     = 0;                    // larghezza in pixel del testo
int endX      = 0;                    // soglia di termine (ultima lettera fuori)
bool tickerActive = false;
unsigned long lastFrame      = 0;
unsigned long lastTickerEnd  = 0;     // quando è FINITO l’ultimo scroll

// ==== Clock ====
unsigned long lastClockRedraw = 0;
const unsigned long CLOCK_REDRAW_MS = 250;

// ==== Net ====
WiFiClient espClient;
PubSubClient mqtt(espClient);
unsigned long lastWiFiAttempt = 0;  const unsigned long WIFI_RETRY_MS = 2000;
unsigned long lastMqttAttempt = 0;  const unsigned long MQTT_RETRY_MS = 3000;
bool ntpConfigured = false;

// ==== Helpers ====
void blAttach() { ledcAttach(PIN_BL, BL_FREQ, BL_BITS); ledcWrite(PIN_BL, BL_STEPS[blIndex]); }
void blNextStep(){ blIndex = (uint8_t)((blIndex + 1) % 4); ledcWrite(PIN_BL, BL_STEPS[blIndex]); }

// Disegno orologio su sprite
void drawClockToSprite() {
  clockSpr.fillSprite(COL_BG);

  struct tm ti;
  if (!getLocalTime(&ti, 0)) {
    clockSpr.setTextDatum(MC_DATUM);
    clockSpr.setTextColor(COL_CLOCK, COL_BG);
    clockSpr.loadFont(bigFont);
    clockSpr.setTextPadding(clockSpr.textWidth("88:88:88") + 20);
    clockSpr.drawString("--:--:--", LCD_W/2, 70);
    clockSpr.unloadFont();

    clockSpr.setTextPadding(0);
    clockSpr.setTextColor(COL_DATE, COL_BG);
    clockSpr.loadFont(font18);
    clockSpr.drawString("NTP...", LCD_W/2, 126);
    clockSpr.unloadFont();
    return;
  }

  char hhmmss[16], dmy[32];
  strftime(hhmmss, sizeof(hhmmss), "%H:%M:%S", &ti);
  strftime(dmy,    sizeof(dmy),    "%a %d %b %Y", &ti);

  clockSpr.setTextDatum(MC_DATUM);
  clockSpr.setTextColor(COL_CLOCK, COL_BG);
  clockSpr.loadFont(bigFont);
  clockSpr.setTextPadding(clockSpr.textWidth("88:88:88") + 20);
  clockSpr.drawString(hhmmss, LCD_W/2, 70);
  clockSpr.unloadFont();

  clockSpr.setTextPadding(0);
  clockSpr.setTextColor(COL_DATE, COL_BG);
  clockSpr.loadFont(font18);
  clockSpr.drawString(dmy, LCD_W/2, 126);
  clockSpr.unloadFont();
}

void pushClock() { clockSpr.pushSprite(0, 0); }

// ==== Ticker FULLSCREEN singolo (usa bigFont) ====
// Pulisce il testo (niente \n) + tronca se troppo lungo
String sanitizeMsg(const String &src) {
  String out = src;

  // Rimpiazzo \n / \r con spazio (HA spesso li mette nei template lunghi)
  out.replace("\r", " ");
  out.replace("\n", " ");

  // Tronco se esageratamente lungo (per sicurezza)
  const int MAX_LEN = 220;   // regolabile
  if (out.length() > MAX_LEN) {
    out = out.substring(0, MAX_LEN - 3);
    out += "...";
  }

  return out;
}

void startTicker(const String &rawMsg) {
  // pulisci il testo
  String clean = sanitizeMsg(rawMsg);

  // salva come ultimo messaggio valido
  lastTickerMsg = clean;
  tickerMsg     = clean;
  if (tickerMsg.isEmpty()) tickerMsg = " ";

  // LOG di debug
  Serial.printf("[Ticker] start, len=%u, text='%s'\n",
                tickerMsg.length(), tickerMsg.c_str());

  tickerSpr.fillSprite(COL_TICKERBG);
  tickerSpr.setTextColor(COL_TICKER, COL_TICKERBG);
  tickerSpr.setTextWrap(false);

  tickerSpr.loadFont(bigFont);
  textW = tickerSpr.textWidth(tickerMsg);
  int fh = tickerSpr.fontHeight();
  int baselineY = (LCD_H - fh) / 2 + fh - 2 + TICKER_Y_OFFSET;
  tickerSpr.unloadFont();

  // Partenza da fuori schermo a destra
  tickerX = LCD_W;
  // Termina quando l'ultima lettera è oltre il bordo sinistro
  endX = -textW - EDGE_MARGIN;

  // Stima durata scroll
  int scrollPixels = (LCD_W + textW + EDGE_MARGIN);
  unsigned long estimated_ms = (unsigned long)((float)scrollPixels / (float)TICKER_SPEED * (float)FRAME_MS);
  Serial.printf("[Ticker] width=%d px, scroll=%d px, speed=%d px/f, dur≈%lu ms\n",
                textW, scrollPixels, TICKER_SPEED, estimated_ms);

  // Primo frame
  tickerSpr.fillSprite(COL_TICKERBG);
  tickerSpr.setTextColor(COL_TICKER, COL_TICKERBG);
  tickerSpr.loadFont(bigFont);
  tickerSpr.setCursor(tickerX, baselineY);
  tickerSpr.print(tickerMsg);
  tickerSpr.unloadFont();
  tickerSpr.pushSprite(0, 0);

  tickerActive   = true;
  lastFrame      = millis();
  // lastTickerEnd sarà aggiornato quando finisce lo scroll
}

void updateTicker() {
  if (!tickerActive) return;

  unsigned long now = millis();
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;

  tickerSpr.fillSprite(COL_TICKERBG);
  tickerSpr.setTextColor(COL_TICKER, COL_TICKERBG);
  tickerSpr.setTextWrap(false);

  tickerSpr.loadFont(bigFont);
  int fh = tickerSpr.fontHeight();
  int baselineY = (LCD_H - fh) / 2 + fh - 2 + TICKER_Y_OFFSET;

  tickerSpr.setCursor(tickerX, baselineY);
  tickerSpr.print(tickerMsg);
  tickerSpr.unloadFont();

  tickerX -= TICKER_SPEED;

  // Fine: quando l'ultima lettera esce a sinistra
  if (tickerX <= endX) {
    tickerActive  = false;
    lastTickerEnd = millis();  // da qui partono i 30s per la prossima ripetizione
    Serial.println("[Ticker] scroll completo, fine messaggio");
  }

  tickerSpr.pushSprite(0, 0);
}

// ==== Net ticks ====
void tickWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastWiFiAttempt < WIFI_RETRY_MS) return;
  lastWiFiAttempt = now;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("[WiFi] connect attempt");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("========== MQTT MESSAGE ==========");
  Serial.printf("[MQTT] topic='%s' len=%u\n", topic, length);

  // Dump HEX
  Serial.print("[MQTT] HEX: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.printf("%02X ", payload[i]);
  }
  Serial.println();

  // Costruisco la String
  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.println("--- RAW STRING ---");
  Serial.println(msg);
  Serial.println("------ END RAW ------");

  startTicker(msg);   // nuovo messaggio, override dell’ultimo
}

void tickMQTT() {
  if (!WiFi.isConnected()) return;
  if (mqtt.connected()) return;
  unsigned long now = millis();
  if (now - lastMqttAttempt < MQTT_RETRY_MS) return;
  lastMqttAttempt = now;

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  String clientId = String("TDisplayS3-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = strlen(MQTT_USER)
            ? mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWD)
            : mqtt.connect(clientId.c_str());
  if (ok) {
    mqtt.subscribe(SUB_TOPIC);
    Serial.printf("[MQTT] connected, subscribed '%s'\n", SUB_TOPIC);
  } else {
    Serial.printf("[MQTT] connect failed, rc=%d\n", mqtt.state());
  }
}

void tickNTP() {
  if (!WiFi.isConnected()) return;
  if (ntpConfigured) return;
  configTzTime(TZ_EU_ZURICH, "pool.ntp.org", "time.nist.gov");
  ntpConfigured = true;
  Serial.println("[NTP] configured");
}

// Bottone brightness (active LOW)
void tickButton() {
  bool nowState = digitalRead(PIN_BTN);
  unsigned long now = millis();
  if (nowState != btnLast && (now - btnLastChange) > BTN_DEBOUNCE_MS) {
    btnLastChange = now;
    btnLast = nowState;
    if (nowState == LOW) {
      blNextStep();
      Serial.printf("[BL] step=%u duty=%u\n", blIndex+1, BL_STEPS[blIndex]);
    }
  }
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PIN_PWR, OUTPUT); digitalWrite(PIN_PWR, HIGH);
  pinMode(PIN_BTN, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);

  // Sprite full-screen
  clockSpr.setColorDepth(16);
  clockSpr.createSprite(LCD_W, LCD_H);

  tickerSpr.setColorDepth(16);
  tickerSpr.createSprite(LCD_W, LCD_H);

  // Backlight PWM
  blAttach();

  // Splash
  clockSpr.fillSprite(COL_BG);
  clockSpr.setTextDatum(TL_DATUM);
  clockSpr.setTextColor(COL_CLOCK, COL_BG);
  clockSpr.loadFont(midleFont);
  clockSpr.drawString("Clock + MQTT (single + repeat 30s)", 10, 28);
  clockSpr.unloadFont();
  clockSpr.pushSprite(0, 0);

  // Net
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setClient(espClient);
}

// ==== LOOP ====
void loop() {
  tickButton();
  tickWiFi();
  tickMQTT();
  if (mqtt.connected()) mqtt.loop();
  tickNTP();

  unsigned long now = millis();

  if (!tickerActive) {
    // Mostra l’orologio
    if (now - lastClockRedraw >= CLOCK_REDRAW_MS) {
      lastClockRedraw = now;
      drawClockToSprite();
      pushClock();
    }

    // Ripeti l’ultimo messaggio ogni 30 secondi dopo la fine dello scroll
    if (!lastTickerMsg.isEmpty() && lastTickerEnd != 0) {
      if (now - lastTickerEnd >= TICKER_REPEAT_MS) {
        Serial.println("[Ticker] repeat ultimo messaggio");
        startTicker(lastTickerMsg);
      }
    }
  } else {
    // Scroll attivo
    updateTicker();
  }
}

/* Home Assistant esempio:
service: mqtt.publish
data:
  topic: t-display/notify
  payload: "PRODUZIONE FV 5.2 kW  •  SOC 83%  •  28°C"
*/
