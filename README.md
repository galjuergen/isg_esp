# isg_esp
A universal isg web based on esp32 which can be used to integrate your Stiebel Eltron heating pump into your smart home system (e.g. Loxone). Tested with WPL 10 AC / WPM 3.

## Acknowledgements

This project is inspired by:
* [http://juerg5524.ch/list_data.php]
* [https://github.com/nopnop2002/esp-idf-can2mqtt]

## Hardware

Tested with following hardware
* LILYGO TTGO T-Internet-POE [https://www.lilygo.cc/products/t-internet-poe]
* SN65HVD230 CAN Transceiver (e.g. Waveshare SN65HVD230) - Attention: In my case i had to unsolder the onboard termination resistance!

## Pin Configuration

### ESP

Pin | Usage
--- | ---
14  | CAN RX
15  | CAN TX

### WPM

Connect to CAN B

## CAN

Baud rate should be set to 50 kbit/s for Wpl 10 AC. Other heating pumps may use other baud rates (e.g. 20 kbit/s)

# MQTT

Following MQTT topics are supported:

## Readig values

Following values are transmitted to the MQTT broker in a cyclic way (configurable via CONFIG_WPM_REQUEST_PERIOD, default: 5s). In order not to overload the bus only one value is retrieved at once.

Topic                            | Description                 | value
---                              | ---                    | ---
wp/read/PROGRAMMSCHALTER         | get current program         | 0-5; 0=Notbetrieb, 1=Bereitschaftsbetrieb, 2=Programmbetrieb, 3=Komfortbetrieb, 4=ECO-Betrieb, 5=Warmwasserbetrieb
wp/read/KUEHLEN_AKTIVIERT        | get cooling active/inactive | 0, 1, on, off
wp/read/SPEICHERISTTEMP          | current warmwater temperature in °C |
wp/read/WPVORLAUFIST             | current "forerun" temperature in °C |
wp/read/RUECKLAUFISTTEMP         | current "trail" temperature in °C |
wp/read/AUSSENTEMP               | current outside temperature in °C |
wp/read/RAUM_IST_TEMPERATUR      | current room temperature in °C |
wp/read/RAUM_SOLL_TEMPERATUR     | target room temperature in °C |
wp/read/RAUM_IST_FEUCHTE         | current room humidity in % |
wp/read/RAUM_TAUPUNKT_TEMPERATUR | current room dew point temperature in °C |
wp/read/WW_SUM_KWH               | total power consumption (KWH) used for warmwater production (has to be added to WW_SUM_MWH) |
wp/read/WW_SUM_MWH               | total power consumption (NWH) used for warmwater production (has to be added to WW_SUM_KWH) |
wp/read/HEIZ_SUM_KWH             | total power consumption (KWH) used for heating (has to be added to HEIZ_SUM_MWH) |
wp/read/HEIZ_SUM_MWH             | total power consumption (MWH) used for heating (has to be added to HEIZ_SUM_KWH) |

## Writing values

Topic                      | Description            | allowed values
---                        | ---                    | ---
wp/write/KUEHLEN_AKTIVIERT | activate cooling       | 0, 1, on, off
wp/write/PROGRAMMSCHALTER  | set current program    | 0-5; 0=Notbetrieb, 1=Bereitschaftsbetrieb, 2=Programmbetrieb, 3=Komfortbetrieb, 4=ECO-Betrieb, 5=Warmwasserbetrieb
wp/write/DATUM             | set current date       | dd.mm.yy
wp/write/TAG               | set current day        | 1-31
wp/write/MONAT             | set current month      | 1-12
wp/write/JAHR              | set current year       | 0-99
wp/write/UHRZEIT           | set current time       | hh:mm

## Build

1. Configure project settings
   > idf.py menuconfig

2. Build
   > idf.py app

3. Flash
   > idf.py app-flash

