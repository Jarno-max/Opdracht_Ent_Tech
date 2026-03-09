/*
 * MCP23S17 Driver - Compact 4x4 Matrix Implementation
 */

#include "mcp23s17.h"
#include "main.h"
#include "tusb.h"

// MCP23S17 registers
#define IODIRA   0x00  // Port A direction (0=output, 1=input)
#define IODIRB   0x01  // Port B direction
#define IOCON    0x0A  // I/O config
#define GPPUA    0x0C  // Port A pull-ups
#define GPPUB    0x0D  // Port B pull-ups
#define MCP_GPIOA    0x12  // Port A data
#define MCP_GPIOB    0x13  // Port B data

// MCP23S17 hardware address (A2..A0). We'll auto-scan on startup.
#ifndef MCP_HW_ADDR
#define MCP_HW_ADDR 0
#endif
static uint8_t mcp_hw_addr = (uint8_t)(MCP_HW_ADDR & 0x07u);

static SPI_HandleTypeDef *spi_handle;
static uint8_t button_state[16] = {0};  // Current state
static uint32_t last_time[16] = {0};    // Debounce timing
static bool mcp_present = false;
static uint8_t mcp_diag = 0; // 0=unknown, 1=ok, 2=fail_ff, 3=fail_other
static bool cols_on_porta = true;   // locked mapping: columns on port A (else on port B)
static uint8_t row_shift = 0;       // 0 for pins 0..3, 4 for pins 4..7
static uint8_t col_shift = 0;       // 0 for pins 0..3, 4 for pins 4..7
static bool matrix_locked = false;  // becomes true after we detect a real key press
static uint32_t matrix_lock_time_ms = 0;

static void SendDebug(uint8_t note)
{
    if (!tud_mounted()) return;
    uint8_t msg_on[3]  = { 0x90, note, 100 };
    uint8_t msg_off[3] = { 0x80, note, 0 };
    tud_midi_stream_write(0, msg_on, 3);
    tud_midi_stream_write(0, msg_off, 3);
}

// Keep SPI transactions short to avoid starving USB (tud_task must run often)
#define MCP_SPI_TIMEOUT_MS  2

// Write register
static bool MCP_Write(uint8_t reg, uint8_t data) {
    uint8_t op = (uint8_t)(0x40u | ((mcp_hw_addr & 0x07u) << 1));
    uint8_t tx[3] = {op, reg, data};
    HAL_GPIO_WritePin(MCP23S17_CS_GPIO_Port, MCP23S17_CS_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef st = HAL_SPI_Transmit(spi_handle, tx, 3, MCP_SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(MCP23S17_CS_GPIO_Port, MCP23S17_CS_Pin, GPIO_PIN_SET);
    return (st == HAL_OK);
}

// Read register
static bool MCP_Read(uint8_t reg, uint8_t *out) {
    uint8_t op = (uint8_t)(0x41u | ((mcp_hw_addr & 0x07u) << 1));
    uint8_t tx[3] = {op, reg, 0x00};
    uint8_t rx[3] = {0};
    HAL_GPIO_WritePin(MCP23S17_CS_GPIO_Port, MCP23S17_CS_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(spi_handle, tx, rx, 3, MCP_SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(MCP23S17_CS_GPIO_Port, MCP23S17_CS_Pin, GPIO_PIN_SET);
    if (st != HAL_OK) return false;
    *out = rx[2];
    return true;
}

// Send MIDI note
static void SendNote(uint8_t note, uint8_t on) {
    if (!tud_mounted()) return;
    uint8_t msg[3] = {on ? 0x90 : 0x80, note, on ? 100 : 0};
    tud_midi_stream_write(0, msg, 3);
}

bool Matrix_IsPresent(void)
{
    return mcp_present;
}

uint8_t Matrix_GetDiag(void)
{
    return mcp_diag;
}

static bool MCP_Probe_Config(uint32_t cpol, uint32_t cpha, uint8_t *readback)
{
    // Re-init SPI with given mode (only used once at startup)
    spi_handle->Init.CLKPolarity = cpol;
    spi_handle->Init.CLKPhase = cpha;
    (void)HAL_SPI_DeInit(spi_handle);
    if (HAL_SPI_Init(spi_handle) != HAL_OK) return false;

    // Robust probe:
    // - If MISO is stuck low, every read returns 0x00 and a naive "write 0, read 0" would pass.
    // - Use non-zero patterns and also verify IOCON.
    uint8_t v = 0x00;

    // HAEN write + readback (IOCON lives at 0x0A/0x0B in BANK=0)
    if (!MCP_Write(IOCON, 0x08)) return false;
    v = 0x00;
    if (!MCP_Read(IOCON, &v)) return false;
    if (v != 0x08)
    {
        if (readback) *readback = v;
        return false;
    }

    // Pattern test on a direction register
    if (!MCP_Write(IODIRA, 0xAA)) return false;
    v = 0x00;
    if (!MCP_Read(IODIRA, &v)) return false;
    if (v != 0xAA)
    {
        if (readback) *readback = v;
        return false;
    }

    if (!MCP_Write(IODIRA, 0x55)) return false;
    v = 0x00;
    if (!MCP_Read(IODIRA, &v)) return false;
    if (readback) *readback = v;
    return (v == 0x55);
}

static uint8_t popcount4(uint8_t v)
{
    v &= 0x0F;
    uint8_t c = 0;
    if (v & 0x01) c++;
    if (v & 0x02) c++;
    if (v & 0x04) c++;
    if (v & 0x08) c++;
    return c;
}

static void MCP_Configure_Matrix(bool columns_on_porta)
{
    // Enable hardware addressing (HAEN). Safe even with single device.
    (void)MCP_Write(IOCON, 0x08);

    if (columns_on_porta)
    {
        // PORTA = outputs (columns), PORTB = inputs (rows)
        (void)MCP_Write(IODIRA, 0x00);
        (void)MCP_Write(IODIRB, 0xFF);
        // Pull-ups on all inputs (robust even if you used B4..B7)
        (void)MCP_Write(GPPUB, 0xFF);
        // Drive all outputs high initially
        (void)MCP_Write(MCP_GPIOA, 0xFF);
    }
    else
    {
        // PORTB = outputs (columns), PORTA = inputs (rows)
        (void)MCP_Write(IODIRB, 0x00);
        (void)MCP_Write(IODIRA, 0xFF);
        // Pull-ups on all inputs (robust even if you used A4..A7)
        (void)MCP_Write(GPPUA, 0xFF);
        // Drive all outputs high initially
        (void)MCP_Write(MCP_GPIOB, 0xFF);
    }
}

static void Matrix_ReportConfig(void)
{
    if (!tud_mounted()) return;
    // CC22=row_shift (0/4), CC23=col_shift (0/4), CC24=cols_on_porta (0/1)
    uint8_t cc1[3] = { 0xB0, 22, row_shift };
    uint8_t cc2[3] = { 0xB0, 23, col_shift };
    uint8_t cc3[3] = { 0xB0, 24, (uint8_t)(cols_on_porta ? 1 : 0) };
    tud_midi_stream_write(0, cc1, 3);
    tud_midi_stream_write(0, cc2, 3);
    tud_midi_stream_write(0, cc3, 3);
}

static void Matrix_ResetDebounce(void)
{
    for (uint8_t i = 0; i < 16; i++)
    {
        button_state[i] = 0;
        last_time[i] = 0;
    }
}

static bool Matrix_TryLockFromScan(bool candidate_cols_on_porta, uint8_t candidate_col_shift)
{
    // Assumes MCP already configured with candidate mapping.
    uint8_t col_reg = candidate_cols_on_porta ? MCP_GPIOA : MCP_GPIOB;
    uint8_t row_reg = candidate_cols_on_porta ? MCP_GPIOB : MCP_GPIOA;

    uint8_t col_mask_all = (uint8_t)(0x0F << candidate_col_shift);

    for (uint8_t col = 0; col < 4; col++)
    {
        // Drive exactly one column low, keep others high, keep non-matrix pins high.
        uint8_t col_mask = (uint8_t)(col_mask_all & (uint8_t)~(1u << (col + candidate_col_shift)));
        col_mask |= (uint8_t)(~col_mask_all);
        if (!MCP_Write(col_reg, col_mask)) return false;

        for (volatile uint32_t i = 0; i < 200; i++) { __NOP(); }

        uint8_t raw = 0;
        if (!MCP_Read(row_reg, &raw)) return false;

        uint8_t low = raw & 0x0F;
        uint8_t high = (raw >> 4) & 0x0F;

        // With pull-ups: idle nibble is 0xF, pressed pulls one or more bits to 0.
        bool low_has_press = (low != 0x0F);
        bool high_has_press = (high != 0x0F);

        if (low_has_press || high_has_press)
        {
            cols_on_porta = candidate_cols_on_porta;
            col_shift = candidate_col_shift;

            if (low_has_press && !high_has_press) row_shift = 0;
            else if (high_has_press && !low_has_press) row_shift = 4;
            else
            {
                // Ambiguous (multiple keys or wiring issue). Pick the nibble with more 0s.
                uint8_t low_zeros = (uint8_t)(4u - popcount4(low));
                uint8_t high_zeros = (uint8_t)(4u - popcount4(high));
                row_shift = (high_zeros > low_zeros) ? 4 : 0;
            }

            matrix_locked = true;
            matrix_lock_time_ms = HAL_GetTick();
            Matrix_ResetDebounce();
            Matrix_ReportConfig();
            SendDebug(86);

            // Restore columns high
            (void)MCP_Write(col_reg, 0xFF);
            return true;
        }
    }

    // Restore columns high
    (void)MCP_Write(col_reg, 0xFF);
    return false;
}

// Initialize MCP23S17 for 4x4 matrix
void Matrix_Init(SPI_HandleTypeDef *hspi) {
    spi_handle = hspi;

    if (spi_handle == NULL) return;

    mcp_present = false;
    mcp_diag = 0;
    matrix_locked = false;
    matrix_lock_time_ms = 0;
    row_shift = 0;
    col_shift = 0;
    cols_on_porta = true;

    // Probe SPI mode + MCP hardware address (A0/A1/A2). This avoids "it works on diag" being dependent
    // on address straps. After a successful probe we apply the full matrix configuration.
    uint8_t last = 0xFF;
    uint8_t found_addr = 0xFF;

    // Electrical sanity check for MISO (PA6):
    // If PA6 reads LOW even with internal pull-up enabled, the line is likely shorted to GND or connected wrong.
    // Report via CC#17 (pull-up read) and CC#18 (pull-down read). Values are 0 or 127.
    {
        (void)HAL_SPI_DeInit(spi_handle);

        GPIO_InitTypeDef gi = {0};
        __HAL_RCC_GPIOA_CLK_ENABLE();
        gi.Pin = GPIO_PIN_6;
        gi.Mode = GPIO_MODE_INPUT;
        gi.Speed = GPIO_SPEED_FREQ_LOW;

        gi.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOA, &gi);
        for (volatile uint32_t i = 0; i < 5000; i++) { __NOP(); }
        uint8_t miso_up = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_SET) ? 127u : 0u;

        gi.Pull = GPIO_PULLDOWN;
        HAL_GPIO_Init(GPIOA, &gi);
        for (volatile uint32_t i = 0; i < 5000; i++) { __NOP(); }
        uint8_t miso_dn = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_SET) ? 127u : 0u;

        if (tud_mounted())
        {
            uint8_t cc17[3] = { 0xB0, 17, miso_up };
            uint8_t cc18[3] = { 0xB0, 18, miso_dn };
            tud_midi_stream_write(0, cc17, 3);
            tud_midi_stream_write(0, cc18, 3);
        }
        // SPI pin will be re-configured to AF by the probe.
    }

    // Try preferred address first, then scan the rest
    for (uint8_t pass = 0; pass < 2 && found_addr == 0xFF; pass++)
    {
        for (uint8_t addr = 0; addr < 8; addr++)
        {
            if (pass == 0)
            {
                addr = (uint8_t)(MCP_HW_ADDR & 0x07u);
            }
            else
            {
                // skip the preferred address (already tried)
                if (addr == (uint8_t)(MCP_HW_ADDR & 0x07u)) continue;
            }

            mcp_hw_addr = (uint8_t)(addr & 0x07u);

            if (MCP_Probe_Config(SPI_POLARITY_LOW,  SPI_PHASE_1EDGE, &last) ||   // Mode 0
                MCP_Probe_Config(SPI_POLARITY_LOW,  SPI_PHASE_2EDGE, &last) ||   // Mode 1
                MCP_Probe_Config(SPI_POLARITY_HIGH, SPI_PHASE_1EDGE, &last) ||   // Mode 2
                MCP_Probe_Config(SPI_POLARITY_HIGH, SPI_PHASE_2EDGE, &last))     // Mode 3
            {
                found_addr = mcp_hw_addr;
                break;
            }

            if (pass == 0) break; // only one try in pass 0
        }
    }

    if (found_addr != 0xFF)
    {
        // Start with a known-safe config. We will auto-lock to the real wiring
        // (port mapping + row/col nibble) when the first key press is detected.
        MCP_Configure_Matrix(true);

        // Report detected address once (CC21)
        if (tud_mounted())
        {
            uint8_t cc_msg[3] = { 0xB0, 21, found_addr };
            tud_midi_stream_write(0, cc_msg, 3);
        }

        mcp_present = true;
        mcp_diag = 1;
        // Init OK
        SendDebug(84);
    }
    else
    {
        // Init failed
        SendDebug(85);

        // Report last readback byte (CC29) for wiring diagnosis (0x00 => MISO stuck low, 0xFF => MISO floating/high)
        if (tud_mounted())
        {
            uint8_t cc_msg[3] = { 0xB0, 29, last };
            tud_midi_stream_write(0, cc_msg, 3);
        }

        // Extra hint: if readback stays 0xFF it's often MISO floating / not connected.
        if (last == 0xFF)
        {
            mcp_diag = 2;
            SendDebug(87);
        }
        else
        {
            mcp_diag = 3;
            SendDebug(88);
        }
    }
}

// Scan 4x4 matrix
void Matrix_Scan(void) {
    static uint32_t last_scan = 0;
    uint32_t now = HAL_GetTick();

    if (spi_handle == NULL) return;
    if (!mcp_present) return;
    if (!tud_mounted()) return;
    
    // Only scan every 10ms to avoid overloading SPI
    if (now - last_scan < 10) return;
    last_scan = now;

    // Force debug CC report (CC30..33) once per second
    static uint32_t last_raw_report_ms = 0;
    bool force_report = false;
    if ((now - last_raw_report_ms) > 1000)
    {
        last_raw_report_ms = now;
        force_report = true;
    }
    
    // If not locked yet, periodically try to lock onto the real wiring.
    // This supports: rows on A or B, and rows/cols on either nibble 0..3 or 4..7.
    if (!matrix_locked)
    {
        static uint32_t last_lock_try_ms = 0;
        static uint8_t lock_state = 0;
        static uint8_t last_candidate_flags = 0; // bit0: col_shift_is_4, bit1: cols_on_porta

        if ((now - last_lock_try_ms) > 60)
        {
            last_lock_try_ms = now;

            bool candidate_cols_on_porta = (lock_state < 2);
            uint8_t candidate_col_shift = (lock_state & 0x01) ? 4 : 0;
            lock_state = (uint8_t)((lock_state + 1) & 0x03);

            last_candidate_flags = (uint8_t)((candidate_col_shift ? 1u : 0u) | (candidate_cols_on_porta ? 2u : 0u));

            MCP_Configure_Matrix(candidate_cols_on_porta);

            // Quick sanity: if the entire row port reads 0x00, something is shorted to GND.
            uint8_t sanity_row = 0;
            uint8_t sanity_reg = candidate_cols_on_porta ? MCP_GPIOB : MCP_GPIOA;
            bool allow_lock = true;
            if (MCP_Read(sanity_reg, &sanity_row))
            {
                if (sanity_row == 0x00)
                {
                    // CC25=1 indicates 'rows stuck low'
                    uint8_t cc_msg[3] = { 0xB0, 25, 1 };
                    tud_midi_stream_write(0, cc_msg, 3);
                    allow_lock = false;
                }
            }

            if (allow_lock)
            {
                (void)Matrix_TryLockFromScan(candidate_cols_on_porta, candidate_col_shift);
            }
        }

        // While searching, don't emit notes yet. Instead report what we see at idle once per second:
        // CC26 = candidate flags (0..3), CC27 = low nibble of row port, CC28 = high nibble of row port.
        static uint32_t last_search_report_ms = 0;
        if ((now - last_search_report_ms) > 1000)
        {
            last_search_report_ms = now;
            bool candidate_cols_on_porta = ((last_candidate_flags & 2u) != 0);
            uint8_t row_reg = candidate_cols_on_porta ? MCP_GPIOB : MCP_GPIOA;
            uint8_t row_raw = 0;
            if (MCP_Read(row_reg, &row_raw))
            {
                uint8_t cc26[3] = { 0xB0, 26, last_candidate_flags };
                uint8_t cc27[3] = { 0xB0, 27, (uint8_t)(row_raw & 0x0F) };
                uint8_t cc28[3] = { 0xB0, 28, (uint8_t)((row_raw >> 4) & 0x0F) };
                tud_midi_stream_write(0, cc26, 3);
                tud_midi_stream_write(0, cc27, 3);
                tud_midi_stream_write(0, cc28, 3);
            }
        }

        return;
    }

    uint8_t col_reg = cols_on_porta ? MCP_GPIOA : MCP_GPIOB;
    uint8_t row_reg = cols_on_porta ? MCP_GPIOB : MCP_GPIOA;

    uint8_t col_mask_all = (uint8_t)(0x0F << col_shift);
    uint8_t col_idle = (uint8_t)(0xFF);

    // Scan each column
    for (uint8_t col = 0; col < 4; col++) {
        // Set column low, others high (only use lower 4 bits)
        uint8_t col_mask = (uint8_t)(col_mask_all & (uint8_t)~(1u << (col + col_shift)));
        // Keep non-matrix pins high
        col_mask |= (uint8_t)(~col_mask_all);
        if (!MCP_Write(col_reg, col_mask)) {
            (void)MCP_Write(col_reg, col_idle);
            mcp_diag = 3;
            return;
        }

        // Small settle time helps with breadboard capacitance / long jumper wires
        for (volatile uint32_t i = 0; i < 200; i++) { __NOP(); }
        
        // Read rows (inverted: pressed = 0, only check lower 4 bits)
        uint8_t raw = 0;
        if (!MCP_Read(row_reg, &raw)) {
            (void)MCP_Write(col_reg, col_idle);
            mcp_diag = 3;
            return;
        }

        // Debug: expose raw row bits per column as CC so wiring issues become visible in MidiView.
        // CC 30..33 carry raw (non-inverted) lower nibble for col 0..3.
        static uint8_t last_raw_cc[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
        uint8_t raw4 = (uint8_t)((raw >> row_shift) & 0x0F);
        if (force_report || (raw4 != last_raw_cc[col]))
        {
            last_raw_cc[col] = raw4;
            uint8_t cc_msg[3] = { 0xB0, (uint8_t)(30 + col), raw4 };
            tud_midi_stream_write(0, cc_msg, 3);
        }

        uint8_t rows = (uint8_t)(~raw4) & 0x0F;
        
        // Check each row
        for (uint8_t row = 0; row < 4; row++) {
            // Button index according to spec: btn = row*4 + col
            uint8_t btn = row * 4 + col;
            uint8_t pressed = (rows >> row) & 1;
            
            // Debounce: 50ms minimum between changes
            if (pressed != button_state[btn] && (now - last_time[btn]) > 50) {
                button_state[btn] = pressed;
                last_time[btn] = now;
                
                // Send MIDI note (60-75)
                SendNote(60 + btn, pressed);

                // Visual feedback on any debounced event
                HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
            }
        }
    }
    
    // Restore all columns high
    (void)MCP_Write(col_reg, col_idle);
}
