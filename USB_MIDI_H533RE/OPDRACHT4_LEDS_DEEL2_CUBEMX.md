# Opdracht 2: Hardware & CubeMX Configuratie (SK6812 LEDs)

## 1. Hardware Aansluitingen
- **Signaal (DIN):** Verbind pin **PB4 (TIM3_CH1)** van de Nucleo met de `DIN` (Data In) van de eerste SK6812 LED.
- **Daisy Chain:** Verbind de `DOUT` van LED 1 met `DIN` van LED 2, `DOUT` van LED 2 met `DIN` van LED 3, etc.
- **Voeding:** Voorzie alle LEDs van **5V** en **GND**. Zorg de GND van de externe 5V voeding ook verbonden is met de GND van de STM32 Nucleo!
- **Condensatoren:** Plaats parallel over de 5V en GND van elke LED een 100 nF ontkoppelcondensator (dichtst bij de VDD pin) om stroompieken op te vangen.
- **Optionele Pull-up (Level shifting):** 
  De STM32 werkt op 3.3V logica, maar de LEDs (op 5V) verwachten soms een hoger HIGH signaal (vaak > 3.5V). 
  Aanpak: Verbind een 1K weerstand tussen PB4 en de 5V (pull-up). Configureer PB4 in CubeMX als *Open Drain* (zie hieronder) in plaats van *Push-Pull*.

---

## 2. CubeMX Configuratie (Wat je MOET aanpassen!)

Open je `.ioc` bestand in STM32CubeIDE en voer de volgende stappen exact uit:

### Stap A: TIM3 Instellen (Voor de 800kHz PWM)
1. Ga aan de linkerkant naar **Timers** en klik op **TIM3**.
2. Zet **Clock Source** op `Internal Clock`.
3. Zet **Channel1** op `PWM Generation CH1`.
4. Ga naar het **Parameter Settings** tabblad onderaan en stel in:
   - **Prescaler (PSC):** `0` (Geen deling, we gebruiken direct de volle kloksnelheid)
   - **Counter Period (ARR):** `312` *(Dit zorgt voor de 800 kHz. STM32H5 klok / 312 = ~800kHz)*
   - **PWM Generation Channel 1:**
     - **Mode:** `PWM mode 1` (Uitgang hoog zolang teller < CCR)
     - **Pulse (CCR1):** `0` (Wordt later door DMA overschreven)
     - **CH Polarity:** `High`

### Stap B: GPDMA1 (Direct Memory Access) Instellen
We gebruiken DMA zodat de CPU niet per bit hoeft in te grijpen.
1. Blijf in het **TIM3** menu, en klik op het tabblad **DMA Settings** (of GPDMA in de H5-serie).
2. Klik op **Add** om een DMA verzoek toe te voegen:
   - **DMA Request:** Selecteer `TIM3_CH1`
   - **Channel:** `GPDMA1 Ch1` (Afhankelijk van wat vrij is, Ch0 wordt waarschijnlijk al door je ADC gebruikt voor de potmeters).
   - **Direction:** `Memory To Peripheral` (Van onze array in RAM naar de Timer sturen).
3. Selecteer het zojuist toegevoegde request en verifieer de parameters onderaan:
   - **Priority:** `Low` (of `Medium`)
   - **Mode:** `Normal` (Stop na volledige transfer, we sturen de LEDs alleen updates als de waarden veranderen).
   - **Increment Address:**
     - Peripheral: **Vink UIT** (Dest Increment: Disable - data moet altijd naar hetzelfde CCR1 register geschreven worden).
     - Memory: **Vink AAN** (Source Increment: Enable - we willen regel voor regel door onze kleuren-array lopen).
   - **Data Width:**
     - Peripheral: `Half Word` (16-bit)
     - Memory: `Half Word` (16-bit)

### Stap C: GPIO Settings voor PB4 nakijken
1. Ga naar **System Core** -> **GPIO** (of check het GPIO tabje bij TIM3).
2. Zoek pin **PB4** (die nu automatisch op `TIM3_CH1` staat).
3. **Belangrijk:** Als je die optionele 1K pull-up weerstand naar 5V gebruikt, verander dan **GPIO output level** of **Maximum output speed** naar Very High en **User Label** eventueel naar `LED_DIN`.
4. Zet de **GPIO mode** op `Alternate Function Open Drain` als je de 5V pull-up hanteert. Als je gewoon direct test met 3.3V (zonder weerstand), laat hem dan op `Alternate Function Push Pull` staan.

---

### Volgende stappen:
Sla het bestand op (`Ctrl+S`) en laat CubeMX de code genereren. Dit maakt de handle voor `htim3` en de DMA klaar. De C-code schrijven we in de volgende opdracht!