# LilyGo T-Display-S3 — Orologio + MQTT Ticker

Orologio NTP con ticker MQTT a scorrimento fullscreen su LilyGo T-Display-S3.

## Descrizione

Firmware per il display AMOLED LilyGo T-Display-S3 (320x170) che mostra un orologio sincronizzato via NTP e riceve messaggi MQTT visualizzati come testo scorrevole a schermo intero. I messaggi vengono ripetuti automaticamente ogni 30 secondi se non ne arrivano di nuovi.

## Funzionalita

- **Orologio NTP** con fuso orario Europe/Zurich (CET/CEST automatico)
- **Ticker MQTT fullscreen** con scorrimento fluido a ~60 fps via sprite anti-flicker
- **5 font inclusi** a diverse dimensioni (bigFont, midleFont, smallFont, tinyFont, font18)
- **4 livelli di luminosita** regolabili tramite il pulsante GPIO0
- **Ripetizione automatica** dell'ultimo messaggio ogni 30 secondi
- **Scorrimento completo** per messaggi lunghi, poi pausa e ripetizione

## Configurazione hardware

| Parametro | Valore |
|-----------|--------|
| Display | AMOLED 320x170 |
| Pin alimentazione | GPIO 15 |
| Pin backlight | GPIO 38 |
| Pin pulsante | GPIO 0 |

## Stack tecnologico

- **Board**: LilyGo T-Display-S3
- **MCU**: ESP32-S3
- **Framework**: Arduino
- **Librerie**: TFT_eSPI, PubSubClient, WiFi
- **Protocollo**: MQTT per ricezione messaggi, NTP per sincronizzazione orario
- **Linguaggio**: C++ (Arduino)

## Struttura

```
lilygotDS3.ino    # Sketch principale
pin_config.h      # Configurazione pin
bigFont.h         # Font grande per ticker
midleFont.h       # Font medio
smallFont.h       # Font piccolo
tinyFont.h        # Font minimo
font18.h          # Font 18px per data
```
