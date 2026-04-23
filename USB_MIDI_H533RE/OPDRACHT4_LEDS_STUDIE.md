# SK6812 (Mini-E) Protocol Studie

## 1. Introductie
De SK6812 Mini-E is een adresseerbare RGB LED met een ingebouwde stroomgestuurde besturingschip. Het datacommunicatieprotocol is compatibel met de bekende WS2812B LED's. Dit betekent dat je ze in een **daisy chain** kunt plaatsen: de `Data Out` (DO) van de ene LED wordt verbonden met de `Data In` (DI) van de volgende. Hierdoor heb je slechts **één enkele datalijn** nodig (naast de 5V voeding en GND) om een hele reeks LEDs aan te sturen.

## 2. Het Protocol: Unipolair RZ (Return-to-Zero)
Het protocol werkt niet met een aparte kloklijn, maar codeert zowel de data (de bits) als de timing-informatie in één signaal. Dit gebeurt via pulsbreedtemodulatie (PWM) ofwel het **Return-to-Zero (RZ)** principe.

Bij het RZ-protocol zendt de datalijn op een vaste frequentie pulsen uit. Bij elke nieuwe bit gaat de lijn eerst omhoog (HIGH) en na een bepaalde tijd weer omlaag (LOW). De verhouding tussen de tijd dat het signaal HIGH is en LOW is, bepaalt of de bit een `0` of een `1` is.

Bij de SK6812 werkt dit (doorgaans op 800 kHz, oftewel een periode van 1.25 µs) als volgt:
- **Bit '0' (T0H + T0L):** De pin is kortstondig HIGH (ca. 0.32 µs) en lange tijd LOW (ca. 0.93 µs).
- **Bit '1' (T1H + T1L):** De pin is relatief lang HIGH (ca. 0.64 µs) en korter LOW (ca. 0.61 µs).

*Opmerking: De specifieke tijden hebben enige tolerantie (bijv. ± 0.15 µs).*

## 3. Data-opbouw per LED
Elke LED verwacht **24 bits** aan data. Deze data bepaalt de helderheid van de individuele kleuren.
De datavolgorde is heel specifiek: **GRB (Groen, Rood, Blauw)**, in plaats van de vaak meer standaard RGB-volgorde.

De volgorde van de 24 bits:
1.  **D23 - D16:** 8 bits voor Groen (G7 is de Most Significant Bit, G0 is LSB)
2.  **D15 - D8:** 8 bits voor Rood
3.  **D7 - D0:** 8 bits voor Blauw

Elke bit wordt met het bovenstaande RZ-protocol verzonden.

## 4. Reset Code
Nadat alle data voor de hele reeks LEDs verstuurd is, moet de bus een tijd laag gehouden worden om aan de LEDs te laten weten dat de huidige "frame" afgerond is en dat ze de nieuwe kleurwaarden moeten doorvoeren (latching).
Voor de SK6812 is een **RESET puls (Treset) van > 80 µs LOW** vereist.

## 5. Aanpak met PWM en DMA (De oplossing in STM32)
Omdat de vereiste nauwkeurigheid in microseconden zo hoog is, kan een CPU niet 'met de hand' (bit-banging) betrouwbaar elke pin hoog en laag maken, zonder constant gehinderd te worden door interrupts (zoals USB of een timer interrupt).
**De oplossing:**
Hiervoor gebruiken we de hardwarematige PWM van een timer op de STM32 in combinatie met DMA (Direct Memory Access).
1. We stellen een timer in op ~800 kHz (PWM periode).
2. We reserveren een array in het geheugen met daarin de pulsbreedtes (PWM 'compare' waarden) voor nullen en enen.
3. De DMA controller pompt deze waarden bit-voor-bit razendsnel naar het hardware PWM-register, precies synchroon zonder dat de CPU dit hoeft te doen.
4. Na het sturen van alle data, sturen we nog een resem '0% duty cycle' pulsen uit via DMA, of we zetten de PWM af om de reset van 80µs+ te garanderen.