# Competition Network: NFC, LoRa, And MQTT

This guide describes the multi-node competition network for a fox hunt.

## Topology

- Slave nodes carry an ESP32, an NFC reader, and an SX127x LoRa radio.
- One master node receives NFC events over LoRa and forwards them to MQTT.
- The MQTT broker can be Mosquitto or another compatible broker on the local network.

The firmware uses the Sandeep Mistry LoRa library, which supports SX1276/77/78/79
radios. Pin profiles are provided for the classic Heltec WiFi LoRa 32, TTGO
LoRa32 V1/V2/V2.1, and T-Beam environments. Heltec WiFi LoRa 32 V3 uses an
SX1262 and is not compatible with this library.

| Hardware | SCK | MISO | MOSI | CS | RESET | DIO0 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Heltec WiFi LoRa 32 / V2 | 5 | 19 | 27 | 18 | 14 | 26 |
| TTGO LoRa32 V1 | 5 | 19 | 27 | 18 | 14 | 26 |
| TTGO LoRa32 V2/V2.1, T-Beam | 5 | 19 | 27 | 18 | 23 | 26 |
| ESP32 DevKit + external SX127x (defaults) | 18 | 19 | 23 | 16 | 17 | 21 |

LoRa and the RC522 share SCK/MISO/MOSI and use separate chip-select pins. The
integrated-board profiles assign RC522 CS=32 and RESET=33. Check the schematic
for the exact board revision before wiring, especially on T-Beam boards where
other onboard peripherals may use additional GPIOs.

## Configure A Slave

Enter these commands in Serial Monitor:

```text
set nfc on
set nfc_tags 04:2A:5C:1D;11:22:33:44
set participant_name runner-01
set mqtt off
set lora on
set lora_role slave
set lora_node node-01
set lora_frequency 868100000
set lora_power 20
```

## Configure The Master

```text
set mqtt on
set wifi_ssid my-network
set wifi_password my-password
set mqtt_broker broker.local
set mqtt_port 1883
set mqtt_topic foxhunt/competition
set lora on
set lora_role master
set lora_node master-01
set lora_frequency 868100000
set lora_power 20
```

All nodes must use the same frequency and sync word. The default sync word is
decimal `18` (`0x12`). LoRa settings are saved immediately but the radio is
initialized at boot, so reboot after changing them. Frequency and transmit
power must comply with local regulations and the radio module's supported band.

These links are not security mechanisms: the LoRa protocol currently has CRC
but no encryption or packet authentication, and an NFC UID can be copied. MQTT
uses plain TCP in this firmware (TLS is not implemented). Use a trusted network
and treat these events as competition logging, not cryptographic proof of
identity or secure access control.

## Verify LoRa

1. Start the master and then the slave.
2. The master should log `LoRa hello from node-01`.
3. The slave should log `LoRa time sync from master-01: network uptime ...`.
4. Scan an allowed NFC tag on the slave. The master should log an accepted NFC
    event, and the slave should log that the event was acknowledged.

Each event has a transaction ID. A slave retries up to five times if it does
not receive an ACK. The master suppresses recent duplicate transactions and
acknowledges an event after it has been accepted for MQTT publication or queued
for later delivery. The slave's LoRa event queue is volatile; an event is
dropped after all retries fail.

SYNC distributes seconds since the master booted; it is not Unix/UTC time.
This firmware does not currently obtain absolute time from GPS or NTP, so MQTT
`timestamp` values are not UTC timestamps.

## Verify MQTT

On a local Mosquitto client, subscribe to the validated-event topic:

```bash
mosquitto_sub -h broker.local -t foxhunt/competition/validated
```

When the master receives an event, the broker receives JSON similar to:

```json
{
   "player": "runner-01",
   "tag": "04:2A:5C:1D",
   "status": "validated",
   "device": "EA5KAO",
   "fox": "MOE",
   "timestamp": "123456789"
}
```

If the broker is unavailable, the master stores events in ESP32 Preferences
and retries MQTT delivery when the broker becomes reachable.

## Hardware Notes

- The SX127x pin profiles above are compile-time defaults. Verify them against
   the exact module and board revision before connecting hardware.
- Use an appropriate antenna and a transmit power permitted in your region.
- Keep the RC522 reader and LoRa antenna physically separated to reduce
   interference.
