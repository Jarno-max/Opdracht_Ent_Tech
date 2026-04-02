# Verslag – USB MIDI Controller met potentiometers (STM32H533RE)

**Student:** Jarno  
**Datum:** 2026  
**Project:** USB MIDI Controller (ADC potentiometers → MIDI CC)  

---

## 1. Inleiding

In dit project bouw ik een USB-MIDI controller op basis van de STM32 Nucleo-H533RE waarbij de belangrijkste bediening gebeurt via potentiometers (“pots”). De microcontroller wordt door de computer herkend als een USB MIDI Class device en stuurt in real-time MIDI Control Change (CC) berichten door op basis van analoge input.

De USB-communicatie wordt geïmplementeerd met TinyUSB. Aan de microcontrollerzijde worden meerdere ADC-kanalen in scan mode ingelezen met DMA, waarna de gemeten waarden geschaald worden naar het MIDI-bereik (0–127). Om ruis en kleine schommelingen te beperken wordt een eenvoudige hysterese toegepast zodat enkel betekenisvolle wijzigingen als CC-bericht verstuurd worden.

### 1.1 Hardware-opstelling (potentiometers)

Elke potentiometer wordt als spanningsdeler aangesloten:
- buitenste pinnen: **3.3 V** en **GND**
- middenpin (wiper): naar een ADC-ingang van de STM32

In deze opdracht worden twee potentiometers gebruikt:
- **POT1** wiper → **PA0 (ADC1_INP0)**
- **POT2** wiper → **PA1 (ADC1_INP1)**

Aanbevolen (optioneel) om ruis/jitter te dempen: een kleine condensator (bv. 10–100 nF) van de wiper naar GND.

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

Na aansluiting via de USER USB-poort verschijnt het apparaat in MIDIview als "JARNO'S MIDI CONTROLLER" — de groene kleur geeft aan dat de verbinding actief is. In de berichtenlijst zijn MIDI-berichten zichtbaar die door de controller verstuurd worden, bijvoorbeeld Control Change berichten voor **CC16** (POT1) en **CC17** (POT2) op kanaal 1 met variërende waardes. Dit bevestigt dat de Nucleo-H533RE correct herkend wordt als USB MIDI Class device en actief MIDI-data doorstuurt naar de computer.

**Figuur 1:** MIDI monitor met meerdere Control Change berichten (minstens CC16 en CC17) tijdens het draaien aan beide potentiometers.

### 2.6 MIDI CC berichten (wat wordt verstuurd?)

De potentiometers sturen **MIDI Control Change** berichten (CC) uit op kanaal 1.

Structuur van een CC bericht (3 bytes):
- Byte 1: status = `0xB0` (Control Change, kanaal 1)
- Byte 2: CC-nummer (0–127)
- Byte 3: waarde (0–127)

Voorbeeld (CC16 met waarde 64): `B0 10 40`.

Opmerking: MIDI-kanalen worden in software vaak als 0–15 gecodeerd. Statusbyte `0xB0` komt overeen met *Control Change op kanaal 1* (kanaal-index 0).

---

## 3. Potentiometers (ADC → MIDI CC)

### 3.1 Doel

Meerdere potentiometers inlezen via de ADC en de gemeten spanning omzetten naar MIDI CC waarden zodat een DAW/MIDI-monitor de veranderingen live kan volgen.

### 3.2 Werking (ADC scan + DMA)

De ADC staat ingesteld op:
- **8-bit resolutie** (0–255)
- **scan mode** met meerdere conversies (meerdere kanalen na elkaar)
- **DMA circular** zodat de buffer automatisch en continu vernieuwd wordt

Concrete configuratie (zoals ingesteld in Cube/HAL):
- ADC1 resolution: **8-bit**
- Regular channels: **ADC_CHANNEL_0 (rank 1)** en **ADC_CHANNEL_1 (rank 2)**
- Sampling time: **24.5 cycles**
- Trigger: **TIM6 TRGO (update event), rising edge**
- `NbrOfConversion = 2` (komt overeen met `POT_COUNT = 2`)

In de code wordt een array gebruikt waarin DMA de waarden plaatst:
```c
volatile uint8_t adc_values[POT_COUNT];
```

**Sampling rate**

TIM6 is ingesteld met prescaler 249 en period 999. Met een TIM6 clock van 250 MHz (APB1 = HCLK = 250 MHz) geeft dit:

$$f_{trigger}=\frac{250\text{ MHz}}{(249+1)(999+1)}\approx 1000\text{ Hz}$$

Elke potentiometer wordt dus met ~1 kHz geüpdatet (per trigger wordt een scan van 2 kanalen uitgevoerd).

### 3.3 Schalen naar MIDI (0–127) + hysterese

Omdat MIDI CC een 7-bit waarde verwacht (0–127), wordt de 8-bit ADC meting geschaald door 1 bit te shiften:
```c
uint8_t new_value = adc_values[i] >> 1;
```

Om te vermijden dat kleine ruis de hele tijd berichten triggert, wordt hysterese gebruikt. Pas als het verschil groter of gelijk is aan `HYSTERESIS`, wordt een nieuwe CC waarde verstuurd.

In deze implementatie is `HYSTERESIS = 2`, wat betekent dat pas bij een verandering van minstens 2 stappen in het 7-bit MIDI domein een nieuw bericht verzonden wordt. Dit vermindert “jitter” in de MIDI monitor bij stilstaande potentiometer.

### 3.4 Overzicht pin-toewijzing (pin → ADC kanaal → CC nummer)

Onderstaande tabel koppelt de hardware (pins) aan de softwareconfiguratie (ADC kanaal) en MIDI output (CC nummer). Dit is een essentieel overzicht voor de evaluatie.

| Pot | STM32 pin | ADC kanaal | Index in `adc_values[]` | MIDI CC |
|---|---|---:|---:|---:|
| POT1 | PA0 | ADC1_INP0 (channel 0) | 0 | 16 |
| POT2 | PA1 | ADC1_INP1 (channel 1) | 1 | 17 |

> Opmerking: de pinnen/kanalen komen uit de CubeMX configuratie en zijn zichtbaar in de ADC MSP init (GPIO analog op PA0 en PA1).

### 3.5 Resultaat (MIDI monitor)

**Figuur 2:** MIDI monitor waar CC16 (POT1) en CC17 (POT2) veranderen wanneer aan de potentiometers gedraaid wordt.

### 3.6 Demonstratievideo

**Video:** voeg hier een link toe (of bestandsnaam in de submission) waarop zichtbaar is dat beide potentiometers CC16 en CC17 sturen en dat de waarden vloeiend mee veranderen.

### 3.7 Test en validatie (hoe toon ik aan dat het werkt?)

Testprocedure (kort, herhaalbaar):
1. Sluit het board aan via de USER USB-poort en open een MIDI monitor.
2. Controleer of het device zichtbaar is als MIDI-input.
3. Draai POT1 traag van min naar max en terug: er moeten CC16 berichten verschijnen met waardes 0–127.
4. Herhaal voor POT2 (CC17).
5. Laat een potentiometer stilstaan: dankzij hysterese mogen er geen (of zeer weinig) “spontane” updates blijven binnenkomen.

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

Het project werkt: de Nucleo-H533RE wordt herkend als USB MIDI device en stuurt CC-berichten op basis van twee potentiometers (CC16 en CC17). Door ADC scan mode met DMA circular te gebruiken kan de firmware meerdere analoge kanalen efficiënt inlezen. Met een timer-trigger van ~1 kHz is de update snel genoeg voor real-time bediening, terwijl de schaalstap naar 0–127 en de hysterese (`HYSTERESIS = 2`) ervoor zorgen dat de MIDI output stabiel is en niet continu overspoeld wordt door kleine meetruis.

### 5.1 Beperkingen en mogelijke verbeteringen

- De mapping gebruikt een eenvoudige bit-shift (8-bit → 7-bit). Dit is snel en voldoende voor MIDI CC, maar geeft geen extra filtering/smoothing.
- Bij zeer ruisrijke opstellingen kan een low-pass filter (RC of softwarematig) de stabiliteit verder verbeteren.
- Een kalibratie (min/max per pot) kan nuttig zijn als niet de volledige 0–3.3 V sweep benut wordt.

---

## 6. Broncode met commentaar (kernstukken)

Dit hoofdstuk beschrijft de belangrijkste codeblokken die nodig zijn om potentiometers als MIDI CC controllers te gebruiken.

De codefragmenten hieronder zijn overgenomen uit de projectbestanden:
- `Core/Src/main.c`
- `Core/Src/usb_descriptors.c`
- `Core/Src/stm32h5xx_hal_msp.c`

De fragmenten zijn soms ingekort (… ) om ze leesbaar te houden, maar de kernlijnen die de werking bepalen zijn volledig weergegeven.

### 6.1 USB initialisatie en main loop

In `main.c` worden eerst de HAL peripherals geïnitialiseerd, daarna TinyUSB. In de main loop is `tud_task()` essentieel: dit verwerkt USB events en moet frequent opgeroepen worden om de verbinding stabiel te houden.

Belangrijkste stappen:
- `tusb_init()` initialiseert TinyUSB
- `tusb_hal_init()` start de USB peripheral
- In de while-loop: `tud_task()` verwerken + applicatietaken (MIDI / pots)

Kernfragment (init + main loop):

```c
// Initialize tinyUSB FIRST
tusb_init();

// Start USB peripheral (HAL) onder TinyUSB
tusb_hal_init();

// Start ADC + timer-trigger
ADC_Start();

while (1)
{
	// TinyUSB device task - MUST be called frequently!
	tud_task();

	// Drain incoming MIDI + LED heartbeat
	midi_task();

	// Verwerk en verzend de potentiometer CC berichten
	process_potentiometer();
}
```

Waarom dit belangrijk is:
- Zonder frequente `tud_task()` kan de USB-communicatie haperen of enumeratie mislukken.
- Door `process_potentiometer()` in de main loop te zetten, worden CC updates continu gestuurd zolang de USB MIDI interface gemount is.

### 6.2 ADC start: timer-trigger + DMA circular

De functie `ADC_Start()` start:
- Timer 6, die de ADC conversie triggert
- ADC1 met DMA in circular mode, die continu `adc_values[]` vult

Waarom DMA?
- De CPU hoeft niet actief te wachten op conversies
- Meerdere kanalen worden automatisch bijgehouden via scan mode

Kernfragment (starten van TIM6 + ADC DMA):

```c
void ADC_Start(void)
{
	// Start the timer to trigger ADC
	HAL_TIM_Base_Start(&htim6);

	// Start ADC conversion on continuous DMA request
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_values, POT_COUNT);
}
```

Kernfragment (TIM6 als triggerbron):

```c
htim6.Instance = TIM6;
htim6.Init.Prescaler = 249;
htim6.Init.Period = 999;
...
sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
```

Kernfragment (ADC scan + externe trigger):

```c
hadc1.Init.Resolution = ADC_RESOLUTION_8B;
hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
hadc1.Init.NbrOfConversion = 2;
hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
hadc1.Init.DMAContinuousRequests = ENABLE;

// Rank 1: channel 0, Rank 2: channel 1
sConfig.Channel = ADC_CHANNEL_0;
sConfig.Rank = ADC_REGULAR_RANK_1;
...
sConfig.Channel = ADC_CHANNEL_1;
sConfig.Rank = ADC_REGULAR_RANK_2;
```

### 6.3 Potentiometer verwerking: schaal, hysterese en CC bericht

De functie `process_potentiometer()` doorloopt alle pot-kanalen. Per kanaal:
1. Schaal ADC (0–255) naar MIDI (0–127) met `>> 1`
2. Bereken absolute afwijking t.o.v. laatst verstuurde waarde
3. Als afwijking ≥ `HYSTERESIS`: stuur een nieuw CC bericht

Definities en buffers (bovenaan `main.c`):

```c
#define HYSTERESIS    2
#define POT_COUNT     2

#define MIDI_CC_POT1  16
#define MIDI_CC_POT2  17

volatile uint8_t adc_values[POT_COUNT];
uint8_t last_midi_values[POT_COUNT] = {0};
```

Kernfragment (verwerking + versturen):

```c
void process_potentiometer(void)
{
	if (!tud_midi_mounted()) return;

	for (uint8_t i = 0; i < POT_COUNT; i++)
	{
		// 8-bit (0-255) -> 7-bit MIDI (0-127)
		uint8_t new_value = adc_values[i] >> 1;

		int16_t diff = (int16_t)new_value - (int16_t)last_midi_values[i];
		if (diff < 0) diff = -diff;

		if (diff >= HYSTERESIS)
		{
			uint8_t cc = pot_cc(i);
			if (cc != 0)
			{
				uint8_t msg[3] = { 0xB0, cc, new_value };
				tud_midi_stream_write(0, msg, 3);
			}
			last_midi_values[i] = new_value;
		}
	}
}
```

Waarom `tud_midi_mounted()`?
- Zo wordt er pas MIDI-data verstuurd nadat de host de MIDI-interface effectief heeft geactiveerd.

### 6.4 CC mapping per potentiometer

De functie `pot_cc(index)` koppelt elk ADC kanaal aan een CC nummer. In deze versie:
- index 0 → CC16
- index 1 → CC17

Kernfragment (mapping via `switch`):

```c
static uint8_t pot_cc(uint8_t index)
{
	switch (index)
	{
		case 0: return MIDI_CC_POT1;
		case 1: return MIDI_CC_POT2;
		default: return 0;
	}
}
```

Bij uitbreiding (meer pots) moet je:
1. `POT_COUNT` verhogen
2. extra ADC kanalen toevoegen in CubeMX
3. extra CC defines toevoegen en `pot_cc()` uitbreiden

### 6.5 USB descriptors: productnaam + MIDI interface

In `usb_descriptors.c` wordt het device als USB MIDI device beschreven (VID/PID, endpoints) en krijgt het een herkenbare productnaam.

Kernfragment (VID/PID + MIDI interface):

```c
#define USB_VID   0xCAFE
#define USB_PID   0x4001

#define EPNUM_MIDI_OUT   0x01
#define EPNUM_MIDI_IN    0x81

uint8_t const desc_fs_configuration[] =
{
	TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
	TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64)
};
```

Kernfragment (string descriptor: productnaam):

```c
char const* string_desc_arr [] =
{
	(const char[]) { 0x09, 0x04 },
	"STMicroelectronics",
	"Jarno's MIDI Controller",
	"123456",
	"TinyUSB MIDI",
};
```

### 6.6 DMA circular: continu buffer verversen

DMA circular mode zorgt ervoor dat de ADC-buffer zonder stop herhaald gevuld wordt. In `stm32h5xx_hal_msp.c` wordt de linked-list DMA in circular mode gezet:

```c
if (HAL_DMAEx_List_SetCircularMode(&List_GPDMA1_Channel0) != HAL_OK)
{
	Error_Handler();
}
```

### 6.7 GPIO/ADC pin initialisatie (PA0 en PA1)

Om de potentiometers correct te kunnen meten moeten de pins op **analog mode** staan zonder pull-up/down. Dit gebeurt in `stm32h5xx_hal_msp.c` binnen `HAL_ADC_MspInit()`:

```c
__HAL_RCC_GPIOA_CLK_ENABLE();
/**ADC1 GPIO Configuration
PA0     ------> ADC1_INP0
PA1     ------> ADC1_INP1
*/
GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
GPIO_InitStruct.Pull = GPIO_NOPULL;
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

Dit fragment sluit rechtstreeks aan bij de hardware-opstelling in hoofdstuk 1.1 (wiper van POT1/POT2 naar PA0/PA1).

