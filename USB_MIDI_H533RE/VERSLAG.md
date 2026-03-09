# Verslag – USB MIDI Controller met Nucleo-H533RE

**Student:** Jarno  
**Datum:** 2026  
**Project:** USB MIDI Keypad Matrix  

---

## 1. Inleiding

In dit project bouw ik een USB MIDI controller op basis van de STM32 Nucleo-H533RE. De controller stuurt MIDI-berichten (Note On / Note Off) naar een computer via USB. In opdracht 1 wordt de basis USB MIDI connectie opgezet. In opdracht 2 wordt een 4×4 knoppenmatrix aangesloten via de MCP23S17 I/O-expander.

---

## 2. Opdracht 1 – USB MIDI Device

### 2.1 Doel

De Nucleo-H533RE moet door de computer herkend worden als een USB MIDI Class device.

### 2.2 Voeding via USB

Standaard wordt de Nucleo gevoed via de ST-Link USB-poort. Voor USB MIDI moet het bord gevoed worden via de **USER USB-poort (CN13)**. Dit vereist de volgende jumper-instelling:

- **JP2 (USB-voeding):** verbind pin 1-2 zodat USB-voeding binnenkomt via de USER USB-poort in plaats van via de ST-Link.

### 2.3 TinyUSB library

Als USB MIDI middleware gebruik ik **TinyUSB** — een open-source USB-stack voor embedded systemen die een kant-en-klare MIDI class driver bevat.

De library staat in `Middlewares/tinyusb/` en wordt beheerd via de Keil MDK RTE.

### 2.4 USB Descriptor

De USB descriptor beschrijft het apparaat aan de host. In `usb_descriptors.c` is een standaard MIDI descriptor opgebouwd met TinyUSB-macro's:

```c
// Vendor ID / Product ID
#define USB_VID   0xCAFE
#define USB_PID   0x4001

// Configuratie descriptor: 1 MIDI interface met IN en OUT endpoint
TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64)
```

De host herkent het apparaat als **USB Audio / MIDI Streaming** device zonder extra drivers.

### 2.5 Resultaat

Na aansluiting via de USER USB-poort verschijnt het apparaat in MIDIview als "JARNO'S MIDI CONTROLLER" — de groene kleur geeft aan dat de verbinding actief is. In de berichtenlijst zijn MIDI-berichten zichtbaar die door de controller verstuurd worden: Controller 20, Channel 1, Value 1 (Hex: B0 14 01). Dit bevestigt dat de Nucleo-H533RE correct herkend wordt als USB MIDI Class device en actief MIDI-data doorstuurt naar de computer.

> *(Voeg hier een screenshot in van MIDI-view met een Note On bericht)*

---

## 3. Opdracht 2 – 4×4 Knoppenmatrix via MCP23S17

### 3.1 Doel

Een 4×4 matrix van drukknopjes aansluiten via de MCP23S17 SPI I/O-expander en elke knop laten corresponderen met een MIDI-noot.

### 3.2 Hardware

**SPI verbinding (STM32 → MCP23S17):**

| STM32 Pin | Functie    | MCP23S17 Pin |
|-----------|------------|--------------|
| PA5       | SPI1_SCK   | 13 (SCK)     |
| PA6       | SPI1_MISO  | 14 (SO)      |
| PA7       | SPI1_MOSI  | 15 (SI)      |
| PA4       | Chip Select| 11 (CS)      |
| 3.3V      | Voeding    | 18 (VDD)     |
| GND       | Massa      | 9 (VSS)      |

**Matrix aansluitingen:**

- **PORTA (GPA0–GPA3):** kolom outputs
- **PORTB (GPB0–GPB3):** rij inputs met interne pull-ups

### 3.3 MCP23S17 Configuratie via SPI

De MCP23S17 wordt geconfigureerd via SPI write-commando's in `mcp23s17.c`:

```c
MCP_Write(IODIRA, 0x00);  // PORTA = alle outputs (kolommen)
MCP_Write(IODIRB, 0xFF);  // PORTB = alle inputs  (rijen)
MCP_Write(GPPUB,  0xFF);  // Pull-ups op alle rij-inputs
MCP_Write(MCP_GPIOA, 0xFF); // Alle kolommen initieel hoog
```

Het SPI-protocol van de MCP23S17 gebruikt een opcode byte:
- **Schrijven:** `0x40 | (adres << 1)`
- **Lezen:**    `0x41 | (adres << 1)`

### 3.4 Scan Algoritme

Het scan algoritme werkt als volgt:

1. Zet één kolom **laag**, alle andere kolommen **hoog**
2. Lees de rij-bits via GPIOB
3. Een bit die **laag** is betekent dat de knop in die rij ingedrukt is
4. Herhaal voor elke kolom

```c
// Stuur kolom 'col' laag, rest hoog
uint8_t col_mask = 0xFF & ~(1 << col);
MCP_Write(MCP_GPIOA, col_mask);

// Lees rijen
uint8_t rows;
MCP_Read(MCP_GPIOB, &rows);

// Bit laag = gedrukt (active-low door pull-ups)
if (!(rows & (1 << row))) {
    // Knop op (col, row) is ingedrukt
}
```

### 3.5 Debouncing

Om contactstuitering te filteren wordt een **20ms tijdvenster** gebruikt per knop:

```c
#define DEBOUNCE_MS 20

if ((HAL_GetTick() - last_time[idx]) > DEBOUNCE_MS) {
    last_time[idx] = HAL_GetTick();
    // verwerk statusverandering
}
```

### 3.6 MIDI Mapping

Elke knop correspondeert met een MIDI-nootnummer via de formule:

$$\text{midi\_noot} = 60 + (\text{rij} \times 4) + \text{kolom}$$

Met basis-noot **60 = Middle C (C4)**:

| | Kolom 0 | Kolom 1 | Kolom 2 | Kolom 3 |
|---|---|---|---|---|
| **Rij 0** | 60 (C4) | 61 (C#4) | 62 (D4) | 63 (D#4) |
| **Rij 1** | 64 (E4) | 65 (F4) | 66 (F#4) | 67 (G4) |
| **Rij 2** | 68 (G#4) | 69 (A4) | 70 (A#4) | 71 (B4) |
| **Rij 3** | 72 (C5) | 73 (C#5) | 74 (D5) | 75 (D#5) |

### 3.7 MIDI berichten versturen

Bij indrukken wordt een **Note On** gestuurd, bij loslaten een **Note Off**:

```c
// Note On
uint8_t msg_on[3]  = { 0x90, midi_note, 100 };
tud_midi_stream_write(0, msg_on, 3);

// Note Off
uint8_t msg_off[3] = { 0x80, midi_note, 0 };
tud_midi_stream_write(0, msg_off, 3);
```

### 3.8 Resultaat

Bij het indrukken van een knop op de matrix verschijnt het bijhorende MIDI-nootnummer in MIDI-view. 

> *(Voeg hier een screenshot of link naar demonstratievideo in)*

---

## 4. Broncode Overzicht

| Bestand | Inhoud |
|---|---|
| `Core/Src/main.c` | Hoofdprogramma, USB + matrix initialisatie en hoofdlus |
| `Core/Src/mcp23s17.c` | MCP23S17 driver: SPI communicatie, scan algoritme, debouncing |
| `Core/Inc/mcp23s17.h` | Header met functie-declaraties |
| `Core/Src/usb_descriptors.c` | USB MIDI descriptor definities |
| `Core/Src/tusb_port.c` | TinyUSB HAL koppeling voor STM32H5 |

---

## 5. Conclusie

Het project werkt: de Nucleo-H533RE wordt herkend als USB MIDI device en de 4×4 knoppenmatrix stuurt de juiste MIDI-noten. De combinatie van TinyUSB (voor USB MIDI) en de MCP23S17 (voor I/O-uitbreiding via SPI) maakt een compacte en werkende MIDI controller mogelijk.
