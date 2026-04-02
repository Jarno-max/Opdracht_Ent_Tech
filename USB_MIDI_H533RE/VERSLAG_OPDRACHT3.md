# Verslag – Opdracht 3: Potentiometers als USB‑MIDI Control Change (STM32H533RE)

**Student:** Jarno  
**Datum:** 2026  
**Vak/Project:** Entertainment Tech – USB MIDI Controller  
**Opdracht 3:** ADC potentiometers → MIDI Control Change (CC)

---

## 1. Doel

Het doel van opdracht 3 is om één of meerdere potentiometers in te lezen met de ADC van de STM32H533RE en de gemeten analoge waarden in real-time om te zetten naar **MIDI Control Change (CC)** berichten via USB.

Concreet:
- continu meten van meerdere analoge kanalen
- ruis/jitter beperken zodat de MIDI-output stabiel blijft
- CC-berichten versturen die zichtbaar zijn in een MIDI monitor/DAW

---

## 2. Hardware-opstelling

### 2.1 Aansluiten van de potentiometers

Elke potentiometer wordt als spanningsdeler aangesloten:
- buitenste pinnen: **3.3 V** en **GND**
- middenpin (wiper): naar een ADC-ingang

In deze uitvoering:
- **POT1 wiper → PA0 (ADC1_INP0)**
- **POT2 wiper → PA1 (ADC1_INP1)**

Optioneel (bij veel jitter): plaats een kleine condensator (bv. 10–100 nF) van wiper naar GND.

### 2.2 USB aansluiting

Voor USB‑MIDI wordt de **USER USB‑poort (CN13)** gebruikt.

---

## 3. MIDI Control Change (CC)

Een MIDI CC bericht bestaat uit 3 bytes:
- **Status**: `0xB0` = Control Change op kanaal 1 (kanaal-index 0)
- **Controller**: CC‑nummer (0–127)
- **Value**: waarde (0–127)

Voorbeeld: CC16 met waarde 64 ⇒ `B0 10 40`.

In deze opdracht:
- POT1 ⇒ **CC16**
- POT2 ⇒ **CC17**

---

## 4. Firmware-opzet (overzicht)

De oplossing bestaat uit 4 delen:
1. USB MIDI device via TinyUSB
2. ADC in **scan mode** (meerdere kanalen)
3. DMA in **circular mode** om continu te updaten zonder CPU polling
4. Omzetting naar MIDI (0–127) + hysterese om ruis te onderdrukken

Belangrijkste bestanden:
- `Core/Src/main.c` (applicatielogica: ADC start + CC versturen)
- `Core/Src/usb_descriptors.c` (USB descriptors + productnaam)
- `Core/Src/stm32h5xx_hal_msp.c` (GPIO analog + DMA init)

---

## 5. Implementatie (code + uitleg)

De codefragmenten hieronder zijn kernstukken uit de projectbestanden. Fragmenten zijn soms ingekort (… ) om leesbaar te blijven.

### 5.1 USB initialisatie en main loop

TinyUSB moet geïnitialiseerd worden en `tud_task()` moet vaak genoeg draaien om USB events te verwerken.

Kernfragment uit `Core/Src/main.c`:

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

Waarom dit werkt:
- `tud_task()` houdt de USB-stack “levend”
- `process_potentiometer()` verstuurt enkel MIDI wanneer de interface gemount is

### 5.2 ADC scan + DMA circular (TIM6 trigger)

De ADC meet meerdere kanalen na elkaar (scan) en DMA schrijft ze automatisch in een buffer.

Start van TIM6 + ADC DMA (`Core/Src/main.c`):

```c
void ADC_Start(void)
{
  HAL_TIM_Base_Start(&htim6);
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_values, POT_COUNT);
}
```

TIM6 instellingen (triggerfrequentie):

```c
htim6.Instance = TIM6;
htim6.Init.Prescaler = 249;
htim6.Init.Period = 999;
...
sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
```

Met een TIM6 clock van 250 MHz resulteert dit in ongeveer:

$$f_{trigger}=\frac{250\text{ MHz}}{(249+1)(999+1)}\approx 1000\text{ Hz}$$

ADC configuratie (scan + externe trigger):

```c
hadc1.Init.Resolution = ADC_RESOLUTION_8B;
hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
hadc1.Init.NbrOfConversion = 2;
hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
hadc1.Init.DMAContinuousRequests = ENABLE;

sConfig.Channel = ADC_CHANNEL_0;
sConfig.Rank = ADC_REGULAR_RANK_1;
...
sConfig.Channel = ADC_CHANNEL_1;
sConfig.Rank = ADC_REGULAR_RANK_2;
```

### 5.3 GPIO analog pins (potentiometer inputs)

De ADC-pinnen moeten in analog mode staan:

```c
__HAL_RCC_GPIOA_CLK_ENABLE();
GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
GPIO_InitStruct.Pull = GPIO_NOPULL;
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

### 5.4 Schalen naar MIDI + hysterese + CC versturen

Definities en buffers in `Core/Src/main.c`:

```c
#define HYSTERESIS    2
#define POT_COUNT     2

#define MIDI_CC_POT1  16
#define MIDI_CC_POT2  17

volatile uint8_t adc_values[POT_COUNT];
uint8_t last_midi_values[POT_COUNT] = {0};
```

Omzetting:
- ADC 8‑bit ⇒ 0–255
- MIDI CC ⇒ 0–127
- Schaling door bitshift: `new_value = adc_values[i] >> 1`

Hysterese:
- pas versturen als verschil ≥ `HYSTERESIS`

Kernfragment `process_potentiometer()`:

```c
void process_potentiometer(void)
{
  if (!tud_midi_mounted()) return;

  for (uint8_t i = 0; i < POT_COUNT; i++)
  {
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

CC mapping via `pot_cc()`:

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

### 5.5 USB descriptor (naam zichtbaar in MIDI monitor)

De productnaam wordt ingesteld in de string descriptors (`Core/Src/usb_descriptors.c`):

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

---

## 6. Test en resultaten

### 6.1 Testprocedure

1. Sluit het board aan via de USER USB‑poort.
2. Open een MIDI monitor.
3. Controleer of het device verschijnt als MIDI input (productnaam: “Jarno’s MIDI Controller”).
4. Draai POT1: er moeten CC16 berichten verschijnen met waardes tussen 0 en 127.
5. Draai POT2: er moeten CC17 berichten verschijnen met waardes tussen 0 en 127.
6. Laat beide potmeters stil staan: er zouden geen (of weinig) extra updates mogen binnenkomen door hysterese.

### 6.2 Bewijs (in te vullen)

- **Figuur 1:** screenshot MIDI monitor met CC16 en CC17 variërend.
- **Figuur 2:** screenshot waarbij een pot stil staat (weinig/geen jitter).
- **Video:** link of bestandsnaam van demonstratie.

---

## 7. Conclusie

De potentiometers worden succesvol ingelezen met ADC scan + DMA circular en in real-time omgezet naar USB‑MIDI Control Change berichten. Door de ADC‑waarden naar 7‑bit te schalen en een hysterese van 2 toe te passen blijft de output stabiel en bruikbaar in een MIDI monitor/DAW.

---

## 8. Mogelijke verbeteringen

- Extra filtering/smoothing (software low‑pass) voor nog vloeiendere CC output.
- Kalibratie per potentiometer (min/max) als de volledige 0–3.3 V range niet gebruikt wordt.
- Uitbreiding naar meer potmeters: `POT_COUNT` en ADC ranks uitbreiden + mapping uitbreiden.
