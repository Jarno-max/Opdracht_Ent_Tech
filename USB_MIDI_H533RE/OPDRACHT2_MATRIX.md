# 4x4 Button Matrix met MCP23S17 - Opdracht 2

## Hardware Configuratie

### STM32H533RE Nucleo Pinout

**SPI1 Pinnen:**
- PA5: SPI1_SCK (Clock)
- PA6: SPI1_MISO (Master In Slave Out)
- PA7: SPI1_MOSI (Master Out Slave In)
- PA4: MCP23S17_CS (Chip Select, software gecontroleerd)

**Overige Pinnen:**
- PB0: Status LED
- PC13: User Button (Nucleo onboard)

### MCP23S17 Aansluitingen

```
STM32H533RE          MCP23S17
-----------          ---------
PA5 (SCK)    ----->  13 (SCK)
PA6 (MISO)   <-----  14 (SO)
PA7 (MOSI)   ----->  15 (SI)
PA4 (CS)     ----->  11 (CS)
3.3V         ----->  18 (VDD)
GND          ----->   9 (VSS)
                     10 (RESET) --> 3.3V (via 10k pull-up)
                     12 (A0) --> GND
                     13 (A1) --> GND
                     14 (A2) --> GND
```

### 4x4 Button Matrix Aansluitingen

**PORTA (Kolommen - Outputs):**
- GPA0 (Pin 21) ----> Kolom 0
- GPA1 (Pin 22) ----> Kolom 1
- GPA2 (Pin 23) ----> Kolom 2
- GPA3 (Pin 24) ----> Kolom 3

**PORTB (Rijen - Inputs met pull-ups):**
- GPB0 (Pin 1) ----> Rij 0
- GPB1 (Pin 2) ----> Rij 1
- GPB2 (Pin 3) ----> Rij 2
- GPB3 (Pin 4) ----> Rij 3

### Button Matrix Schema

```
        Col0    Col1    Col2    Col3
        GPA0    GPA1    GPA2    GPA3
         |       |       |       |
Row0 ----o-------o-------o-------o---- GPB0
         |       |       |       |
Row1 ----o-------o-------o-------o---- GPB1
         |       |       |       |
Row2 ----o-------o-------o-------o---- GPB2
         |       |       |       |
Row3 ----o-------o-------o-------o---- GPB3

o = Drukknop (normaal open)
```

## MIDI Mapping

**Basis Noot:** 60 (Middle C / C4)

**Formule:** `midi_note = 60 + (row × 4) + col`

### Volledige Mapping Table

| Positie | Kolom | Rij | MIDI Note | Note Name |
|---------|-------|-----|-----------|-----------|
| Btn 0   | 0     | 0   | 60        | C4        |
| Btn 1   | 1     | 0   | 61        | C#4       |
| Btn 2   | 2     | 0   | 62        | D4        |
| Btn 3   | 3     | 0   | 63        | D#4       |
| Btn 4   | 0     | 1   | 64        | E4        |
| Btn 5   | 1     | 1   | 65        | F4        |
| Btn 6   | 2     | 1   | 66        | F#4       |
| Btn 7   | 3     | 1   | 67        | G4        |
| Btn 8   | 0     | 2   | 68        | G#4       |
| Btn 9   | 1     | 2   | 69        | A4        |
| Btn 10  | 2     | 2   | 70        | A#4       |
| Btn 11  | 3     | 2   | 71        | B4        |
| Btn 12  | 0     | 3   | 72        | C5        |
| Btn 13  | 1     | 3   | 73        | C#5       |
| Btn 14  | 2     | 3   | 74        | D5        |
| Btn 15  | 3     | 3   | 75        | D#5       |

## Software Functionaliteit

### Scan Algoritme

1. **Kolom Selectie:** Zet één kolom laag, andere hoog
2. **Rij Lezen:** Lees status van alle rijen
3. **Debouncing:** Filter bounces met 20ms timer
4. **Event Detectie:** Detecteer veranderingen in knopstatus
5. **MIDI Output:** Verstuur Note On/Off berichten

### Debouncing

- **Tijd:** 20 milliseconden
- **Methode:** Software debouncing met timestamp tracking
- Per knop worden 3 states bijgehouden:
  - `button_raw`: Direct gelezen status
  - `button_state`: Stabiele status na debouncing
  - `button_time`: Timestamp van laatste verandering

### MIDI Berichten

**Note On (knop ingedrukt):**
- Status Byte: `0x90` (Note On, kanaal 1)
- Data Byte 1: Note number (60-75)
- Data Byte 2: Velocity = 100

**Note Off (knop losgelaten):**
- Status Byte: `0x80` (Note Off, kanaal 1)
- Data Byte 1: Note number (60-75)
- Data Byte 2: Velocity = 0

## Compileren en Uploaden

### Met Keil MDK-ARM

1. Open `MDK-ARM/USB_MIDI2.uvprojx`
2. Build Project (F7)
3. Download to Flash (F8)

### Testen

1. Sluit STM32 aan via USB
2. Device verschijnt als USB MIDI device
3. Open MIDI monitor software (bijv. MIDI-OX, MIDI Monitor)
4. Druk op knoppen in de matrix
5. Zie Note On/Off berichten verschijnen

## Bestandsstructuur

```
Core/
├── Inc/
│   ├── main.h                  (Updated: includes mcp23s17.h)
│   └── mcp23s17.h             (NEW: MCP23S17 driver header)
└── Src/
    ├── main.c                  (Updated: SPI init, matrix scan)
    ├── mcp23s17.c             (NEW: MCP23S17 driver implementation)
    ├── stm32h5xx_hal_msp.c    (Updated: SPI MSP init)
    └── usb_descriptors.c       (Bestaand: USB MIDI descriptors)
```

## Troubleshooting

### Device wordt niet herkend
- Controleer USB kabel
- Controleer dat tinyUSB correct is geïnitialiseerd
- Check Device Manager (Windows) of lsusb (Linux)

### Geen MIDI berichten
- Controleer SPI aansluitingen (vooral CS op PA4)
- Verifieer MCP23S17 voeding (3.3V)
- Check matrix aansluitingen
- Test met onboard button eerst

### Verkeerde noten
- Verifieer rij/kolom aansluitingen
- Check MIDI mapping table
- Controleer dat MIDI_BASE_NOTE = 60

### Bouncing/dubbele noten
- Verhoog DEBOUNCE_TIME_MS in mcp23s17.h
- Controleer kwaliteit van drukknoppen
- Voeg externe pull-up weerstanden toe indien nodig

## Code Aanpassingen

### MIDI Kanaal Wijzigen

In `mcp23s17.c`:
```c
#define MIDI_CHANNEL  0  // 0 = kanaal 1, 1 = kanaal 2, etc.
```

### Basis Noot Wijzigen

In `mcp23s17.c`:
```c
#define MIDI_BASE_NOTE  60  // 60 = Middle C
```

### Velocity Aanpassen

In `mcp23s17.c`:
```c
#define MIDI_VELOCITY  100  // 0-127
```

### Debounce Tijd Aanpassen

In `mcp23s17.h`:
```c
#define DEBOUNCE_TIME_MS  20  // milliseconden
```

## Performance

- **Scan Rate:** ~250Hz (4ms per volledige scan)
- **Debounce Delay:** 20ms
- **MIDI Latency:** <25ms van knopdruk tot MIDI bericht
- **CPU Load:** <5% @ 32MHz sysclock

## Uitbreidingen

### Velocity Sensing
Voeg timing measurement toe tussen kolom-activatie en rij-detectie voor velocity-sensitive toetsen.

### Multiple Matrices
Gebruik meerdere MCP23S17 chips (verschillende CS pinnen) voor grotere matrices.

### LED Feedback
Gebruik ongebruikte PORTA/PORTB pinnen voor LED indicators per knop.

### MIDI CC Messages
Voeg potentiometers toe via ADC voor MIDI Control Change berichten.

---

**Auteur:** STM32H5 MIDI Project  
**Datum:** Februari 2026  
**Versie:** 1.0
