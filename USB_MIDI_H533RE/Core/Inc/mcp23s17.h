/*
 * MCP23S17 SPI I/O Expander Driver for 4x4 Button Matrix
 * Compact implementation for 32KB code limit
 */

#ifndef MCP23S17_H
#define MCP23S17_H

#include "stm32h5xx_hal.h"
#include <stdbool.h>

// Function prototypes
void Matrix_Init(SPI_HandleTypeDef *hspi);
void Matrix_Scan(void);
bool Matrix_IsPresent(void);
uint8_t Matrix_GetDiag(void);

#endif // MCP23S17_H
