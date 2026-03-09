# Toets Voorbereiding - USB MIDI Keypad Matrix

---

## 1. Algemeen overzicht

| | |
|---|---|
| **Microcontroller** | STM32 Nucleo-H533RE |
| **USB stack** | TinyUSB (open-source, in `Middlewares/tinyusb/`) |
| **I/O expander** | MCP23S17 (SPI, 16-bit I/O, 28-pin DIP) |
| **Matrix** | 4x4 drukknopjes = 16 knoppen |
| **Communicatie** | USB MIDI Class (geen driver nodig op PC) |

### Wat is MIDI?
MIDI (Musical Instrument Digital Interface) is een communicatieprotocol voor muziekinstrumenten. Het stuurt **commando's** (geen audio), zoals "druk noot 60 in met velocity 100". Een MIDI-bericht bestaat uit 3 bytes:
- **Byte 1**: statusbyte - wat voor bericht (Note On, Note Off, Controller...)
- **Byte 2**: parameter 1 (b.v. nootnummer 0-127)
- **Byte 3**: parameter 2 (b.v. velocity/sterkte 0-127)

### Wat is USB MIDI Class?
USB heeft een ingebouwde klasse voor MIDI-apparaten (onderdeel van de USB Audio klasse). Hierdoor herkent Windows/Mac het apparaat **automatisch zonder extra drivers**. TinyUSB zorgt voor de volledige implementatie van dit protocol.

### Wat is een I/O expander?
De STM32 heeft een beperkt aantal GPIO-pinnen. De MCP23S17 voegt via SPI **16 extra GPIO-pinnen** toe (PORTA + PORTB, elk 8 bits). Zo kunnen we een 4x4 matrix (8 pinnen nodig) aansluiten zonder de STM32-pinnen op te gebruiken.

---

## 2. Opdracht 1 - USB MIDI Device

### Jumper instelling
- **JP2 pin 1-2** verbinden -> voeding via **USER USB-poort (CN13)**, niet via ST-Link
- Waarom? De USER USB-poort is verbonden met de USB peripheral van de STM32H533. De ST-Link poort is enkel voor programmeren/debuggen. Als je via de verkeerde poort voedt, werkt USB MIDI niet.

### TinyUSB
- Bibliotheek staat in `Middlewares/tinyusb/`
- TinyUSB is event-driven: `tud_task()` verwerkt inkomende USB-events en stuurt data door. Als je deze niet regelmatig aanroept, "ziet" de host het apparaat als verbroken.
- Initialisatie in `main.c`:
  ```c
  tusb_init();       // TinyUSB initialiseren (registreert alle callbacks)
  tusb_hal_init();   // USB peripheral van de STM32 starten
  // In de hoofdlus:
  tud_task();        // MOET regelmatig aangeroepen worden - verwerkt USB events
  ```

### USB Descriptor (`usb_descriptors.c`)
Een USB descriptor is een datastructuur die de host vertelt **wat voor apparaat** er aangesloten is. De host leest dit uit bij de eerste verbinding (enumeratie).

```c
#define USB_VID   0xCAFE   // Vendor ID  (wie maakt het apparaat)
#define USB_PID   0x4001   // Product ID (welk product is het)
#define USB_BCD   0x0200   // USB versie 2.0

// Endpoints - kanalen voor data uitwisseling
#define EPNUM_MIDI_OUT   0x01   // Host -> Device (ontvangen van PC)
#define EPNUM_MIDI_IN    0x81   // Device -> Host (versturen naar PC)

// Descriptor opbouw
TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100)
TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64)
```
- **VID/PID**: identificeert het apparaat (zoals een vingerafdruk). Windows slaat de driver op bij het eerste gebruik.
- **Endpoint**: een logisch kanaal op de USB-bus. MIDI_IN (0x81) = data van STM32 naar PC. MIDI_OUT (0x01) = data van PC naar STM32.
- **64 bytes**: maximale pakketgrootte per USB-transactie.
- Host herkent het als **USB Audio / MIDI Streaming device** - geen driver nodig.

### `tud_mounted()` vs `tud_connected()`
| Functie | Betekenis |
|---------|-----------|
| `tud_connected()` | USB-kabel is ingeplugd (elektrisch signaal aanwezig) |
| `tud_mounted()` | Host heeft het apparaat volledig geenumereerd en is klaar voor gebruik |

In de code wachten we op `tud_mounted()` voordat we MIDI sturen of de matrix starten.

### MIDI berichten versturen
```c
// Note On (knop ingedrukt) - 3 bytes
uint8_t msg_on[3]  = { 0x90, midi_note, 100 };
tud_midi_stream_write(0, msg_on, 3);

// Note Off (knop losgelaten) - 3 bytes
uint8_t msg_off[3] = { 0x80, midi_note, 0 };
tud_midi_stream_write(0, msg_off, 3);
```
| Byte | Waarde | Betekenis |
|------|--------|-----------|
| 0 | `0x90` | Note On, MIDI kanaal 1 |
| 0 | `0x80` | Note Off, MIDI kanaal 1 |
| 1 | `midi_note` | Nootnummer (0-127), 60 = Middle C |
| 2 | `100` | Velocity (aanslagsterkte) bij Note On |
| 2 | `0` | Velocity 0 bij Note Off |

> **Tip:** `0x90` met velocity 0 is ook geldig als Note Off in MIDI, maar in deze code wordt expliciet `0x80` gebruikt.

---

## 3. Opdracht 2 - 4x4 Matrix via MCP23S17

### Wat is SPI?
SPI (Serial Peripheral Interface) is een serieel communicatieprotocol met 4 draden:
| Signaal | Richting | Betekenis |
|---------|----------|-----------|
| **SCK** | Master -> Slave | Kloksignaal - bepaalt de snelheid |
| **MOSI** | Master -> Slave | Data van STM32 naar MCP23S17 |
| **MISO** | Slave -> Master | Data van MCP23S17 naar STM32 |
| **CS** | Master -> Slave | Chip Select - laag = dit apparaat is actief |

De STM32 is de **master**, de MCP23S17 is de **slave**. CS moet laag zijn tijdens een transactie.

### Hardware verbindingen

#### SPI (STM32 -> MCP23S17)
| STM32 Pin | Functie      | MCP23S17 Pin | Uitleg |
|-----------|--------------|--------------|--------|
| PA5       | SPI1_SCK     | 12 (SCK)     | Klok |
| PA6       | SPI1_MISO    | 14 (SO)      | Data van MCP -> STM32 |
| PA7       | SPI1_MOSI    | 13 (SI)      | Data van STM32 -> MCP |
| **PA8**   | Chip Select  | 11 (CS)      | Actief laag, software aangestuurd |
| 3.3V      | Voeding      | 9 (VDD)      | |
| GND       | Massa        | 10 (VSS)     | |

#### Adrespinnen (adres = 0)
De MCP23S17 heeft 3 adrespinnen (A0/A1/A2) zodat je **tot 8 stuks** op een SPI-bus kunt zetten. Elk krijgt een ander adres (0-7).
| MCP23S17 Pin | Aansluiting | Adresbit |
|--------------|-------------|----------|
| 15 (A0)      | GND         | bit 0 = 0 |
| 16 (A1)      | GND         | bit 1 = 0 |
| 17 (A2)      | GND         | bit 2 = 0 |
| 18 (RESET)   | 3.3V        | Altijd actief (laag = reset) |

#### Matrix aansluitingen
- **GPA0-GPA3** (PORTA): kolom **outputs** - de STM32 zet deze hoog/laag
- **GPB0-GPB3** (PORTB): rij **inputs** met interne pull-ups - de STM32 leest deze uit

### MCP23S17 SPI Protocol - gedetailleerd
Elke SPI-transactie bestaat altijd uit **3 bytes**:
```
Byte 1: Opcode   = 0x40 | (adres << 1) | R/W-bit
                   ^^^^                   ^^^^^^^
                   vast                   0=schrijven, 1=lezen

Byte 2: Register = welk register (b.v. 0x00 voor IODIRA)
Byte 3: Data     = te schrijven waarde, of 0x00 bij lezen (genegeerd)
```

**Voorbeelden (adres=0):**
```
Schrijven naar IODIRA:  0x40  0x00  0x00   -> PORTA alle outputs
Lezen van GPIOB:        0x41  0x13  0x00   -> leest rij-bits terug, antwoord in byte 3
```

### Registers
| Register | Adres | Functie | Voorbeeld |
|----------|-------|---------|-----------|
| IODIRA   | 0x00  | Richting PORTA: **0** = output, **1** = input | `0x00` = allemaal output |
| IODIRB   | 0x01  | Richting PORTB | `0xFF` = allemaal input |
| IOCON    | 0x0A  | I/O configuratie | `0x08` = HAEN bit aan |
| GPPUA    | 0x0C  | Pull-up weerstanden PORTA (1 = pull-up aan) | |
| GPPUB    | 0x0D  | Pull-up weerstanden PORTB | `0xFF` = alle pull-ups aan |
| GPIOA    | 0x12  | Lees/schrijf data PORTA | |
| GPIOB    | 0x13  | Lees/schrijf data PORTB | |

### Initialisatie van de matrix
```c
MCP_Write(IOCON,     0x08);  // HAEN bit aan: adrespinnen A0/A1/A2 actief
MCP_Write(IODIRA,    0x00);  // PORTA = outputs (sturen kolommen aan)
MCP_Write(IODIRB,    0xFF);  // PORTB = inputs  (lezen rijen uit)
MCP_Write(GPPUB,     0xFF);  // Pull-ups op PORTB: idle toestand is HIGH
MCP_Write(MCP_GPIOA, 0xFF);  // Start met alle kolommen HIGH (inactief)
```

### Hoe werkt een knoppenmatrix?
Een 4x4 matrix gebruikt **4 kolom-draden** en **4 rij-draden** in plaats van 16 aparte draden.
- Elke knop zit op het kruispunt van 1 kolom en 1 rij.
- Door 1 kolom laag te zetten en de rijen te lezen, zie je welke knoppen in die kolom ingedrukt zijn.
- Dit herhaal je voor elke kolom -> zo scan je alle 16 knoppen met slechts 8 draden.

```
        Kolom 0  Kolom 1  Kolom 2  Kolom 3
Rij 0     K00      K01      K02      K03
Rij 1     K10      K11      K12      K13
Rij 2     K20      K21      K22      K23
Rij 3     K30      K31      K32      K33
```

### Scan algoritme - stap voor stap
```c
for (col = 0; col < 4; col++) {
    // Stap 1: Zet kolom 'col' laag, alle andere kolommen hoog
    uint8_t col_mask = 0xFF & ~(1 << col);
    // col=0: col_mask = 0b11111110 = 0xFE  (bit 0 laag)
    // col=1: col_mask = 0b11111101 = 0xFD  (bit 1 laag)
    MCP_Write(MCP_GPIOA, col_mask);

    // Stap 2: Lees de rijen uit
    uint8_t rows;
    MCP_Read(MCP_GPIOB, &rows);
    // rows = b.v. 0b11111011 -> bit 2 is laag -> rij 2 is ingedrukt

    // Stap 3: Controleer elke rij
    for (row = 0; row < 4; row++) {
        if (!(rows & (1 << row))) {
            // Bit 'row' is laag = knop ingedrukt
            // (active-low: pull-up houdt het hoog, knop trekt het naar GND)
        }
    }
}
// Stap 4: Herstel alle kolommen hoog
MCP_Write(MCP_GPIOA, 0xFF);
```

### Debouncing - waarom en hoe
Een mechanische drukknop "stuitert" bij indrukken: het contact maakt en verbreekt snel meerdere keren achter elkaar (binnen ~10ms). Zonder debounce denkt de software dat je de knop tientallen keren indrukt.

**Oplossing:** wacht minimaal 50ms tussen twee statuswijzigingen van dezelfde knop.
```c
if (pressed != button_state[btn] && (HAL_GetTick() - last_time[btn]) > 50) {
    button_state[btn] = pressed;      // nieuwe toestand opslaan
    last_time[btn] = HAL_GetTick();   // tijdstip van laatste verandering opslaan
    SendNote(60 + btn, pressed);      // MIDI sturen
}
```
- `HAL_GetTick()` geeft de tijd in milliseconden sinds opstarten.
- `button_state[btn]` houdt de **vorige** toestand bij (0 = losgelaten, 1 = ingedrukt).
- Pas als de toestand **verandert EN** er 50ms verstreken zijn, wordt de MIDI boodschap verstuurd.

### MIDI Mapping
Formule: `midi_noot = 60 + (rij x 4) + kolom`

- **60** = Middle C (C4) als basisnoot
- Elke rij verhoogt de noot met 4, elke kolom met 1

|       | Kolom 0   | Kolom 1   | Kolom 2   | Kolom 3   |
|-------|-----------|-----------|-----------|-----------|
| Rij 0 | 60 (C4)   | 61 (C#4)  | 62 (D4)   | 63 (D#4)  |
| Rij 1 | 64 (E4)   | 65 (F4)   | 66 (F#4)  | 67 (G4)   |
| Rij 2 | 68 (G#4)  | 69 (A4)   | 70 (A#4)  | 71 (B4)   |
| Rij 3 | 72 (C5)   | 73 (C#5)  | 74 (D5)   | 75 (D#5)  |

**Voorbeeld:** knop op rij 2, kolom 3 -> `60 + (2x4) + 3 = 60 + 8 + 3 = 71` = B4

---

## 4. Hoofdlus structuur (`main.c`)

```c
while (1) {
    tud_task();           // TinyUSB verwerken - ALTIJD als eerste!

    if (tud_mounted()) {  // Pas starten met matrix na USB-verbinding
        Matrix_Scan();    // Matrix uitlezen en MIDI sturen
    }
}
```

### Opstartsequentie (wat gebeurt er na inpluggen?)
1. STM32 start op, `HAL_Init()` en klok configuratie
2. `tusb_init()` + `tusb_hal_init()` -> USB peripheral geactiveerd
3. Host detecteert nieuw apparaat -> **enumeratie**: host vraagt descriptors op
4. `tud_mounted()` wordt `true` -> USB klaar
5. Na 250ms wacht: `Matrix_Init()` -> SPI probe van MCP23S17
6. Als MCP gevonden: `matrix_started = true` -> `Matrix_Scan()` actief
7. Elke knopdruk -> MIDI Note On/Off naar PC

> **Belangrijk:** `tud_task()` moet zo vaak mogelijk aangeroepen worden. Als dit te lang geblokkeerd wordt (b.v. door trage SPI), verliest de USB verbinding.

---

## 5. Bestandsoverzicht

| Bestand | Inhoud |
|---------|--------|
| `Core/Src/main.c` | Hoofdprogramma, initialisatie, hoofdlus |
| `Core/Src/mcp23s17.c` | MCP23S17 driver: SPI communicatie, scan algoritme, debouncing |
| `Core/Inc/mcp23s17.h` | Header met functie-declaraties (`Matrix_Init`, `Matrix_Scan`, ...) |
| `Core/Src/usb_descriptors.c` | USB MIDI descriptor (VID, PID, endpoints) |
| `Core/Src/tusb_port.c` | TinyUSB HAL koppeling: verbindt TinyUSB met de STM32H5 USB hardware |
| `Core/Inc/main.h` | Pin definities (o.a. `MCP23S17_CS_Pin = GPIO_PIN_8`) |

---

## 6. Veelgestelde vragen (mogelijke toetsvragen)

**Waarom PA8 en niet PA4 als CS?**
-> In `main.h` is `MCP23S17_CS_Pin = GPIO_PIN_8` op `GPIOA` geconfigureerd via STM32CubeMX. PA4 staat in de opdrachttabel maar de effectieve configuratie in de code is PA8.

**Waarom eerst `tud_task()` en dan pas `Matrix_Scan()`?**
-> USB heeft prioriteit. De host verwacht regelmatige antwoorden. Als SPI-communicatie te lang duurt en `tud_task()` niet opgeroepen wordt, time-out de USB verbinding aan de kant van de host.

**Wat is active-low?**
-> Met pull-ups ingeschakeld op PORTB is de idle toestand van elke rij-pin HIGH (3.3V). Wanneer een knop ingedrukt wordt, verbindt hij de rij-pin met de kolom-pin die laag gezet is -> de rij-pin wordt LOW. Dus: LOW = ingedrukt, HIGH = losgelaten.

**Wat doet de HAEN bit in IOCON?**
-> Hardware Address Enable. Als HAEN = 1, reageert de MCP23S17 alleen op SPI-commando's die zijn hardwareadres bevatten (ingesteld via A0/A1/A2). Als HAEN = 0, reageert hij op elk adres. HAEN instellen is best practice, zeker als je meerdere MCP23S17's op een bus hebt.

**Wat is het verschil tussen IODIRA en GPIOA?**
-> IODIRA stelt de **richting** in (input of output). GPIOA leest of schrijft de **data**. Je moet eerst de richting instellen via IODIRA, daarna kun je via GPIOA de pinnen lezen of aansturen.

**Waarom 250ms wachten voor `Matrix_Init()`?**
-> Na `tud_mounted()` is de USB-verbinding logisch klaar, maar de host (Windows/Mac) heeft nog een korte tijd nodig om de driver te laden. SPI-verkeer onmiddellijk na mount kan de USB-verbinding verstoren.

**Waarom alle kolommen hoog zetten na de scan?**
-> Als je een kolom laag laat staan en meerdere knoppen tegelijk ingedrukt zijn, kunnen rij-pinnen via de matrix onbedoeld naar GND getrokken worden (ghost keypresses). Door alles hoog te zetten na de scan is de matrix in rust.

**Wat is het verschil tussen Note On met velocity 0 en Note Off?**
-> In het MIDI-protocol is `0x90` + velocity `0` officieel equivalent aan Note Off. In deze code wordt `0x80` (expliciete Note Off) gebruikt voor duidelijkheid, maar beide zijn correct.

**Hoe bereken je de opcode voor adres 3?**
-> Schrijven: `0x40 | (3 << 1) = 0x40 | 0x06 = 0x46`
-> Lezen:     `0x41 | (3 << 1) = 0x41 | 0x06 = 0x47`

**Wat als RESET (pin 18) niet aan 3.3V hangt?**
-> De MCP23S17 blijft in reset en reageert niet op SPI. De chip is actief-laag reset: pin 18 laag = chip in reset, pin 18 hoog = chip actief.

**Wat is enumeratie?**
-> Het proces waarbij de USB-host voor het eerst het apparaat herkent: hij vraagt de descriptors op (device descriptor, configuration descriptor), kent een adres toe en laadt de juiste driver. Dit gebeurt automatisch bij het inpluggen.

---

## 7. WAT MOET JE KUNNEN UITLEGGEN? (per hoofdstuk)

> Dit zijn de exacte leerdoelen uit het toetsdocument, per hoofdstuk beantwoord.

---

### Hoofdstuk 1 - USB Hardware Setup (1.3)

**Hoe wordt de Nucleo-H533RE gevoed voor USB MIDI?**
-> Via de **USER USB-poort (CN13)**. Jumper JP2 moet op pin 1-2 staan zodat de voeding via de USER USB-poort binnenkomt. De ST-Link poort (CN14) dient enkel voor programmeren en debuggen, niet voor USB MIDI.

**Wat is het verschil tussen de ST-Link USB-poort en de USER USB-poort?**
-> De ST-Link poort (CN14) is verbonden met de ST-Link debugger chip en wordt gebruikt om code te flashen en te debuggen. De USER USB-poort (CN13) is rechtstreeks verbonden met de USB peripheral van de STM32H533 microcontroller en wordt gebruikt voor USB toepassingen zoals USB MIDI.

**Welke USB-klasse gebruikt dit project en waarom zijn er geen drivers nodig?**
-> Dit project gebruikt de **USB MIDI Class**, die deel uitmaakt van de USB Audio klasse. Windows en macOS hebben ingebouwde ondersteuning voor deze klasse, waardoor het apparaat automatisch herkend wordt zonder extra drivers te installeren.

**Wat is TinyUSB en waarvoor dient het?**
-> TinyUSB is een open-source USB-stack voor embedded systemen. Het implementeert het volledige USB-protocol inclusief de MIDI class driver, zodat je als ontwikkelaar enkel `tud_midi_stream_write()` hoeft te roepen om MIDI data te sturen zonder je zelf bezig te houden met USB packets, endpoints of descriptors.

---

### Hoofdstuk 2 - MIDI USB Class (2.4)

**Wat is MIDI en wat stuurt het door?**
-> MIDI (Musical Instrument Digital Interface) is een communicatieprotocol voor digitale muziekinstrumenten. Het stuurt **commando's** (geen audio), zoals "druk noot X in" of "laat noot X los". Elk MIDI-bericht bestaat uit 3 bytes: statusbyte, data byte 1, data byte 2.

**Leg de structuur uit van een Note On en Note Off bericht.**
```
Note On:   [ 0x90 ] [ nootnummer ] [ velocity ]
Note Off:  [ 0x80 ] [ nootnummer ] [ 0        ]

0x90 = 1001 0000 -> hoge nibble 0x9 = Note On, lage nibble 0x0 = kanaal 1
0x80 = 1000 0000 -> hoge nibble 0x8 = Note Off, lage nibble 0x0 = kanaal 1
```
- Nootnummer 60 = Middle C (C4), range 0-127
- Velocity = aanslagsterkte, 0-127 (0 bij Note Off)

**Wat is een USB endpoint? Wat is het verschil tussen MIDI_IN en MIDI_OUT?**
-> Een USB endpoint is een logisch kanaal voor dataoverdracht tussen host en apparaat.
- **MIDI_OUT (0x01)**: data stroomt van de PC (host) naar de STM32 (device) - b.v. om noten te ontvangen
- **MIDI_IN (0x81)**: data stroomt van de STM32 (device) naar de PC (host) - b.v. om knopdrukken te sturen
Het `0x80` bit in het endpointnummer geeft de richting aan: 0x80 = IN (naar host).

**Wat is een USB descriptor en wat staat erin?**
-> Een USB descriptor is een datastructuur die de host vertelt wat voor apparaat er op de bus zit. Bij de eerste verbinding (enumeratie) vraagt de host de descriptor op. Dit project heeft:
- **Device descriptor**: VID (0xCAFE), PID (0x4001), USB versie (2.0), fabrikant/productnaam
- **Configuration descriptor**: stromverbruik (100mA), aantal interfaces
- **MIDI descriptor**: omschrijft de MIDI interface met IN en OUT endpoint (pakketgrootte 64 bytes)

**Wat is VID/PID?**
-> Vendor ID (VID) en Product ID (PID) zijn 16-bit getallen die samen het apparaat uniek identificeren. Windows slaat bij de eerste verbinding de driver op gekoppeld aan dit VID/PID. VID 0xCAFE is een test-VID voor prototypes (niet officieel geregistreerd).

---

### Hoofdstuk 3 - SPI Communicatie met MCP23S17 (3.5)

**Leg het SPI protocol uit. Welke 4 signalen zijn er?**
-> SPI (Serial Peripheral Interface) communiceert via 4 draden:
| Signaal | Beschrijving |
|---------|-------------|
| **SCK** (klok) | Gegenereerd door de master, synchroniseert alle communicatie |
| **MOSI** | Master Out Slave In - data van STM32 naar MCP23S17 |
| **MISO** | Master In Slave Out - data van MCP23S17 naar STM32 |
| **CS** | Chip Select, actief laag - selecteert welke slave actief is |

SPI is **full-duplex**: master en slave sturen tegelijk data in beide richtingen.

**Hoe verloopt een SPI-transactie met de MCP23S17?**
-> Elke transactie bestaat uit precies **3 bytes**, terwijl CS laag is:
1. **Byte 1 - Opcode**: `0x40 | (adres << 1)` voor schrijven, `0x41 | (adres << 1)` voor lezen
2. **Byte 2 - Register**: welk register je wil lezen/schrijven (b.v. 0x12 voor GPIOA)
3. **Byte 3 - Data**: de te schrijven waarde (bij schrijven) of 0x00 (bij lezen, antwoord in ontvangen byte 3)

**Wat is de MCP23S17 en waarvoor dient hij?**
-> De MCP23S17 is een SPI I/O expander met 16 extra GPIO-pinnen (PORTA: GPA0-GPA7 en PORTB: GPB0-GPB7). Hij lost het probleem op dat de STM32 te weinig vrije GPIO-pinnen heeft voor een 4x4 matrix. Via slechts 4 SPI-draden + CS krijg je 16 extra pinnen.

**Leg de registers IODIRA/B, GPPUA/B en GPIOA/B uit.**
- **IODIRA/B**: richting register. Bit = 0 -> output, Bit = 1 -> input. Stel dit ALTIJD in voor je GPIOA/B gebruikt.
- **GPPUA/B**: pull-up register. Bit = 1 -> interne pull-up weerstand actief op die pin. Nodig voor active-low knoppen.
- **GPIOA/B**: data register. Schrijven -> zet output pin hoog/laag. Lezen -> geeft huidige status van input pins terug.

**Wat doet de HAEN bit in het IOCON register?**
-> Hardware Address Enable. Als HAEN = 1 (IOCON = 0x08), activeert de MCP23S17 zijn adrespinnen A0/A1/A2. Het hardwareadres in de opcode (bits [3:1]) moet dan overeenkomen met de fysieke stand van A0/A1/A2. Dit maakt het mogelijk om tot 8 MCP23S17's op 1 SPI-bus te zetten met elk een uniek adres.

---

### Hoofdstuk 4 - Button Debouncing (4.4)

**Wat is button bouncing en waarom is het een probleem?**
-> Mechanische drukknopjes zijn niet perfect: bij het indrukken of loslaten "stuitert" het mechanisch contact meerdere keren in snel tempo (binnen ~1-20ms). De microcontroller is zo snel dat hij die stuiteringen interpreteert als meerdere afzonderlijke knopdrukken, terwijl de gebruiker maar 1 keer drukte. Voor MIDI zou dit meerdere ongewenste Note On/Off berichten sturen.

**Welke debounce methode wordt gebruikt in dit project?**
-> Een **tijdgebaseerde debounce** per knop: na een toestandsverandering (ingedrukt/losgelaten) worden nieuwe veranderingen voor diezelfde knop genegeerd gedurende **50ms**.
```c
if (pressed != button_state[btn] && (HAL_GetTick() - last_time[btn]) > 50) {
    button_state[btn] = pressed;
    last_time[btn] = HAL_GetTick();
    SendNote(60 + btn, pressed);
}
```

**Leg uit hoe toestandsdetectie werkt voor MIDI (edge detection).**
-> De code houdt voor elke knop de vorige toestand bij (`button_state[btn]`). Alleen wanneer de toestand **verandert** (van 0 naar 1, of van 1 naar 0) wordt er een MIDI bericht gestuurd:
- 0 -> 1 (losgelaten -> ingedrukt): stuurt **Note On**
- 1 -> 0 (ingedrukt -> losgelaten): stuurt **Note Off**

Zonder toestandsdetectie zou de code bij elke scan (elke 10ms) opnieuw een Note On sturen zolang de knop ingedrukt is.

**Wat is het verschil tussen polling en interrupt-gebaseerde debounce?**
-> In dit project wordt **polling** gebruikt: de matrix wordt elke 10ms gescand in de hoofdlus. Bij interrupt-gebaseerde debounce zou een hardware interrupt afgaan bij elke flank op een GPIO, maar dat is complexer met een I/O expander via SPI. Polling is eenvoudiger en voldoende voor MIDI (50ms reactietijd is onmerkbaar voor een muzikant).

---

### Hoofdstuk 5 - Keyboard Matrix Scanning (5.4)

**Waarom gebruik je een matrix in plaats van directe verbindingen?**
-> Een 4x4 matrix van 16 knoppen aansluiten met directe verbindingen vereist 16 GPIO-pinnen. Met een matrix volstaan **8 pinnen** (4 kolommen + 4 rijen). Dit bespaart pinnen op de I/O expander en maakt grotere matrices mogelijk (b.v. 8x8 = 64 knoppen met slechts 16 pinnen).

**Leg stap voor stap uit hoe de matrix gescand wordt.**
1. Zet kolom 0 **laag**, alle andere kolommen **hoog** (`GPIOA = 0xFE`)
2. Lees de 4 rijen via `GPIOB`
3. Een rij-bit die **laag** is = knop op dat kruispunt is ingedrukt (active-low via pull-ups)
4. Herhaal voor kolom 1 (`GPIOA = 0xFD`), kolom 2 (`0xFB`), kolom 3 (`0xF7`)
5. Herstel alle kolommen **hoog** (`GPIOA = 0xFF`)

**Wat is ghost pressing en hoe wordt het vermeden?**
-> Ghost pressing treedt op als meerdere knoppen tegelijk ingedrukt zijn en de matrix per ongeluk extra knoppen "ziet" die niet ingedrukt zijn. Dit omdat stroomwegen via meerdere ingedrukte knoppen en rij-draden tot kortsluitingen leiden. Oplossing: alle kolommen steeds terug naar hoog zetten na de scan, en enkel 1 kolom tegelijk laag zetten.

**Bereken het MIDI-nootnummer voor knop op rij 3, kolom 2.**
-> `midi_noot = 60 + (rij x 4) + kolom = 60 + (3 x 4) + 2 = 60 + 12 + 2 = 74` = D5

**Waarom wordt er een kleine pauze ingevoegd na het schrijven naar de kolom en voor het lezen van de rijen?**
-> Na het laag zetten van een kolom duurt het een fractie van een seconde voordat het signaal stabiel is over de draad (capacitantie van bedrading en breadboard). De kleine pauze (`__NOP()` lussen) geeft het signaal tijd om te stabiliseren zodat de lezing een correcte waarde geeft.

**Hoe communiceert de STM32 met de MCP23S17 tijdens het scannen?**
-> Bij elke kolomstap zijn er **2 SPI-transacties van elk 3 bytes**:
1. `MCP_Write(GPIOA, col_mask)` -> stuurt `[0x40][0x12][col_mask]` over SPI (kolom laag zetten)
2. `MCP_Read(GPIOB, &rows)` -> stuurt `[0x41][0x13][0x00]` en ontvangt de rij-status in byte 3

Voor 4 kolommen = 8 SPI-transacties per scan. Bij een scan elke 10ms = 800 SPI-transacties per seconde, wat ruimschoots binnen de SPI-capaciteit van de STM32 valt.
