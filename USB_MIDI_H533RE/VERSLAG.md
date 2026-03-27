# Verslag – USB MIDI Controller met potentiometers (STM32H533RE)

**Student:** Jarno  
**Datum:** 2026  
**Project:** USB MIDI Controller (ADC potentiometers → MIDI CC)  

---

## 1. Inleiding

In dit project bouw ik een USB-MIDI controller op basis van de STM32 Nucleo-H533RE waarbij de belangrijkste bediening gebeurt via potentiometers (“pots”). De microcontroller wordt door de computer herkend als een USB MIDI Class device en stuurt in real-time MIDI Control Change (CC) berichten door op basis van analoge input.

De USB-communicatie wordt geïmplementeerd met TinyUSB. Aan de microcontrollerzijde worden meerdere ADC-kanalen in scan mode ingelezen met DMA, waarna de gemeten waarden geschaald worden naar het MIDI-bereik (0–127). Om ruis en kleine schommelingen te beperken wordt een eenvoudige hysterese toegepast zodat enkel betekenisvolle wijzigingen als CC-bericht verstuurd worden.

---

## 2. USB MIDI Device (TinyUSB)

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

> *(Voeg hier een screenshot in van je MIDI monitor met meerdere CC berichten van meerdere potentiometers. Bijvoorbeeld: CC16 en CC17 met variërende waardes.)*

### 2.6 MIDI CC berichten (wat wordt verstuurd?)

De potentiometers sturen **MIDI Control Change** berichten (CC) uit op kanaal 1.

Structuur van een CC bericht (3 bytes):
- Byte 1: status = `0xB0` (Control Change, kanaal 1)
- Byte 2: CC-nummer (0–127)
- Byte 3: waarde (0–127)

Voorbeeld (CC16 met waarde 64): `B0 10 40`.

---

## 3. Potentiometers (ADC → MIDI CC)

### 3.1 Doel

Meerdere potentiometers inlezen via de ADC en de gemeten spanning omzetten naar MIDI CC waarden zodat een DAW/MIDI-monitor de veranderingen live kan volgen.

### 3.2 Werking (ADC scan + DMA)

De ADC staat ingesteld op:
- **8-bit resolutie** (0–255)
- **scan mode** met meerdere conversies (meerdere kanalen na elkaar)
- **DMA circular** zodat de buffer automatisch en continu vernieuwd wordt

In de code wordt een array gebruikt waarin DMA de waarden plaatst:
```c
volatile uint8_t adc_values[POT_COUNT];
```

### 3.3 Schalen naar MIDI (0–127) + hysterese

Omdat MIDI CC een 7-bit waarde verwacht (0–127), wordt de 8-bit ADC meting geschaald door 1 bit te shiften:
```c
uint8_t new_value = adc_values[i] >> 1;
```

Om te vermijden dat kleine ruis de hele tijd berichten triggert, wordt hysterese gebruikt. Pas als het verschil groter of gelijk is aan `HYSTERESIS`, wordt een nieuwe CC waarde verstuurd.

### 3.4 Overzicht pin-toewijzing (pin → ADC kanaal → CC nummer)

Onderstaande tabel koppelt de hardware (pins) aan de softwareconfiguratie (ADC kanaal) en MIDI output (CC nummer). Dit is een essentieel overzicht voor de evaluatie.

| Pot | STM32 pin | ADC kanaal | Index in `adc_values[]` | MIDI CC |
|---|---|---:|---:|---:|
| POT1 | PA0 | ADC1_INP0 (channel 0) | 0 | 16 |
| POT2 | PA1 | ADC1_INP1 (channel 1) | 1 | 17 |

> Opmerking: de pinnen/kanalen komen uit de CubeMX configuratie en zijn zichtbaar in de ADC MSP init (GPIO analog op PA0 en PA1).

### 3.5 Resultaat (MIDI monitor)

> *(Voeg hier een screenshot toe van je MIDI monitor waarop je tegelijk CC16 en CC17 ziet veranderen wanneer je aan de potentiometers draait.)*

### 3.6 Demonstratievideo

> *(Voeg hier een link toe naar je video of verwijs naar het bestand dat je indient.)*

---

## 4. Broncode Overzicht

| Bestand | Inhoud |
|---|---|
| `Core/Src/main.c` | Hoofdprogramma, TinyUSB init, ADC (DMA) start en versturen van MIDI CC |
| `Core/Src/usb_descriptors.c` | USB descriptors (VID/PID, endpoints, productnaam) |
| `Core/Src/tusb_port.c` | TinyUSB HAL koppeling voor STM32H5 |
| `Core/Src/stm32h5xx_hal_msp.c` | Hardware init: ADC pins + DMA configuratie |

---

## 5. Conclusie

Het project werkt: de Nucleo-H533RE wordt herkend als USB MIDI device en stuurt CC-berichten op basis van meerdere potentiometers. Door ADC scan mode met DMA te gebruiken kan de firmware meerdere analoge kanalen efficiënt inlezen. De schaalstap naar 0–127 en de hysterese zorgen ervoor dat de MIDI output stabiel is en niet continu overspoeld wordt door kleine meetruis.

---

## 6. Broncode met commentaar (kernstukken)

Dit hoofdstuk beschrijft de belangrijkste codeblokken die nodig zijn om potentiometers als MIDI CC controllers te gebruiken. De volledige broncode zit in de bijlage/zip van het project; hieronder worden de relevante functies en keuzes toegelicht.

### 6.1 USB initialisatie en main loop

In `main.c` worden eerst de HAL peripherals geïnitialiseerd, daarna TinyUSB. In de main loop is `tud_task()` essentieel: dit verwerkt USB events en moet frequent opgeroepen worden om de verbinding stabiel te houden.

Belangrijkste stappen:
- `tusb_init()` initialiseert TinyUSB
- `tusb_hal_init()` start de USB peripheral
- In de while-loop: `tud_task()` verwerken + applicatietaken (MIDI / pots)

### 6.2 ADC start: timer-trigger + DMA circular

De functie `ADC_Start()` start:
- Timer 6, die de ADC conversie triggert
- ADC1 met DMA in circular mode, die continu `adc_values[]` vult

Waarom DMA?
- De CPU hoeft niet actief te wachten op conversies
- Meerdere kanalen worden automatisch bijgehouden via scan mode

### 6.3 Potentiometer verwerking: schaal, hysterese en CC bericht

De functie `process_potentiometer()` doorloopt alle pot-kanalen. Per kanaal:
1. Schaal ADC (0–255) naar MIDI (0–127) met `>> 1`
2. Bereken absolute afwijking t.o.v. laatst verstuurde waarde
3. Als afwijking ≥ `HYSTERESIS`: stuur een nieuw CC bericht

Het CC bericht is:
```c
uint8_t msg[3] = { 0xB0, cc, new_value };
tud_midi_stream_write(0, msg, 3);
```

### 6.4 CC mapping per potentiometer

De functie `pot_cc(index)` koppelt elk ADC kanaal aan een CC nummer. In deze versie:
- index 0 → CC16
- index 1 → CC17

Bij uitbreiding (meer pots) moet je:
1. `POT_COUNT` verhogen
2. extra ADC kanalen toevoegen in CubeMX
3. extra CC defines toevoegen en `pot_cc()` uitbreiden

