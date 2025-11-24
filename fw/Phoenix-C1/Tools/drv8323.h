#ifndef DRV8323_H
#define DRV8323_H

#include <stdint.h>
#include <stdbool.h>

// NOTE: Adjust this include to your actual HAL header, e.g.:
#include "stm32gxxx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------
// Public driver context
// ----------------------------------------------
typedef struct
{
	SPI_HandleTypeDef *hspi;

	GPIO_TypeDef *enable_port;
	uint16_t      enable_pin;   // nSLEEP / ENABLE pin

	GPIO_TypeDef *hiz_port;
	uint16_t      hiz_pin;      // MOTOR_HIZ (controls INLA/B/C together)

	GPIO_TypeDef *fault_port;
	uint16_t      fault_pin;    // nFAULT input (active low)

	TIM_HandleTypeDef *htim_pwm;
	uint32_t          tim_channel_u;  // PWM channel for INHA
	uint32_t          tim_channel_v;  // PWM channel for INHB
	uint32_t          tim_channel_w;  // PWM channel for INHC

	// Last commanded normalized duties for diagnostics [-1, 1]
	float duty_u;
	float duty_v;
	float duty_w;

	bool initialized;
} drv8323_t;

// ----------------------------------------------
// DRV8323 register addresses (see datasheet)
// Note: DRV8323 uses 11-bit data fields. Address space 0x00..0x06 typical.
// ----------------------------------------------
#define DRV8323_REG_FAULT_STATUS_1   0x00u
#define DRV8323_REG_VGS_STATUS_2     0x01u
#define DRV8323_REG_DRV_CTRL         0x02u
#define DRV8323_REG_GATE_DRV_HS      0x03u
#define DRV8323_REG_GATE_DRV_LS      0x04u
#define DRV8323_REG_OCP_CTRL         0x05u
#define DRV8323_REG_CSA_CTRL         0x06u

// ----------------------------------------------
// Library-level fault mask (interpreted from status registers)
// These are aggregate software flags for convenience
// ----------------------------------------------
#define DRV8323_FAULT_NONE           0x0000u
#define DRV8323_FAULT_VDS_OCP        0x0001u  // Any VDS overcurrent (any HS/LS FET)
#define DRV8323_FAULT_GDF            0x0002u  // Gate driver fault
#define DRV8323_FAULT_UVLO           0x0004u  // Undervoltage lockout
#define DRV8323_FAULT_OTSD           0x0008u  // Overtemperature shutdown
#define DRV8323_FAULT_VGS_HS         0x0010u  // VGS fault HS side
#define DRV8323_FAULT_VGS_LS         0x0020u  // VGS fault LS side
#define DRV8323_FAULT_CPUV            0x0040u  // Charge pump undervoltage
#define DRV8323_FAULT_OC_FAULTS      0x0080u  // Other overcurrent related flags

// ----------------------------------------------
// Public API
// ----------------------------------------------
/**
 * Initialize the driver context with provided hardware handles and pins.
 * This does not configure the PWM timer; it assumes it is already set up.
 */
void drv8323_init(drv8323_t *drv,
				  SPI_HandleTypeDef *hspi,
				  GPIO_TypeDef *enable_port, uint16_t enable_pin,
				  GPIO_TypeDef *hiz_port,    uint16_t hiz_pin,
				  GPIO_TypeDef *fault_port,  uint16_t fault_pin,
				  TIM_HandleTypeDef *htim_pwm,
				  uint32_t tim_channel_u,
				  uint32_t tim_channel_v,
				  uint32_t tim_channel_w);

/**
 * Configure DRV8323S with safe default register settings via SPI.
 * Assumes the device is powered. It will bring nSLEEP high and then write registers.
 */
HAL_StatusTypeDef drv8323_configure_default(drv8323_t *drv);

/** Enable (exit nSLEEP) the driver. */
void drv8323_enable(drv8323_t *drv);

/** Disable (enter nSLEEP) the driver. Also forces Hi-Z for safety. */
void drv8323_disable(drv8323_t *drv);

/**
 * Control global Hi-Z through MOTOR_HIZ pin.
 * hiz = true  => all phases Hi-Z (coast)
 * hiz = false => all phases active
 */
void drv8323_set_hiz(drv8323_t *drv, bool hiz);

/**
 * Clear driver latched faults via SPI (and optionally by a brief nSLEEP toggle).
 */
HAL_StatusTypeDef drv8323_clear_faults(drv8323_t *drv);

/**
 * Read faults from DRV8323 status registers and return a library-level bitmask.
 */
HAL_StatusTypeDef drv8323_read_faults(drv8323_t *drv, uint16_t *fault_mask);

/**
 * Set 3-phase normalized duties in range [-1.0, 1.0] for U/V/W.
 * Mapping: duty = 0.5f * (value + 1.0f) => [-1,1] -> [0,1].
 * Intended for SPWM/FOC upper control providing phase references.
 */
void drv8323_set_duty_uvw(drv8323_t *drv, float u, float v, float w);

// Optional low-level register accessors
HAL_StatusTypeDef drv8323_write_reg(drv8323_t *drv, uint8_t addr, uint16_t value);
HAL_StatusTypeDef drv8323_read_reg(drv8323_t *drv, uint8_t addr, uint16_t *value);

#ifdef __cplusplus
}
#endif

#endif // DRV8323_H

