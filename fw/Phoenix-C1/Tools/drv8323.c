#include "drv8323.h"

#include <string.h>

// ----------------------------------------------
// DRV8323 SPI framing
// 16-bit frame: [15]=R/W (1=read,0=write), [14:11]=addr, [10:0]=data
// Big-endian transfer: MSB first
// ----------------------------------------------
#define DRV8323_SPI_RW_READ        (0x8000u)
#define DRV8323_SPI_ADDR_SHIFT     (11u)
#define DRV8323_SPI_ADDR_MASK      (0x0Fu)
#define DRV8323_SPI_DATA_MASK      (0x07FFu)

// ----------------------------------------------
// Selected register field helpers (verify with datasheet for your build)
// These symbolic values aim to be reasonable defaults; adjust to your design.
// ----------------------------------------------
// DRV_CTRL (0x02)
#define DRV8323_DRVCTRL_CLR_FLT    (1u << 0)  // Clear faults latch when written as 1
// Optional feature bits (datasheet specific):
#define DRV8323_DRVCTRL_DIS_GDF    (1u << 1)  // Disable gate driver fault (example)
#define DRV8323_DRVCTRL_OTW_REP    (1u << 2)  // Report OTW (example)
// PWM mode bits example (3x PWM). Check datasheet PWM_MODE field position.
#define DRV8323_DRVCTRL_PWM_MODE_3 (0x2u << 5)

// GATE_DRV_HS (0x03), GATE_DRV_LS (0x04) fields are device-specific.
// Provide moderate drive strengths and TDRIVE = 500ns as safe defaults.
#define DRV8323_IDRIVEP_50mA       (0x5u)     // example code
#define DRV8323_IDRIVEN_100mA      (0x6u)     // example code
#define DRV8323_TDRIVE_500ns       (0x2u)     // example code

// OCP_CTRL (0x05): Set overcurrent to a moderate threshold, latch shutdown.
#define DRV8323_OCP_DEG_4us        (0x1u)     // example code
#define DRV8323_OCP_MODE_LATCH     (0x2u)     // example code
#define DRV8323_VDS_LVL_MID        (0x6u)     // example mid-level VDS threshold

// CSA_CTRL (0x06): Set CSA gain and reference to a common value (20 V/V, 0.25V)
#define DRV8323_CSA_GAIN_20V       (0x2u)     // example code
#define DRV8323_CSA_FET_LOW_SIDE   (0x0u)     // example code
#define DRV8323_CSA_VREF_0V25      (0x0u)     // example code (depends on VREF selection)
#define DRV8323_CSA_CAL_CH1_OFF    (0x0u)     // example code

// Convenience timeout for SPI transactions (ms)
#ifndef DRV8323_SPI_TIMEOUT_MS
#define DRV8323_SPI_TIMEOUT_MS     10u
#endif

// ----------------------------------------------
// Internal helpers
// ----------------------------------------------
static inline float drv8323_clampf(float x, float lo, float hi)
{
	if (x < lo) return lo;
	if (x > hi) return hi;
	return x;
}

static uint32_t drv8323_get_tim_arr(TIM_HandleTypeDef *htim)
{
#if defined(__HAL_TIM_GET_AUTORELOAD)
	return __HAL_TIM_GET_AUTORELOAD(htim);
#else
	return htim->Init.Period;
#endif
}

// Perform a single 16-bit SPI transaction.
// Assumes hardware NSS managed by SPI peripheral.
static HAL_StatusTypeDef drv8323_spi_transfer(drv8323_t *drv, uint16_t tx, uint16_t *rx)
{
	if (drv == NULL || drv->hspi == NULL) {
		return HAL_ERROR;
	}

	uint8_t txb[2];
	uint8_t rxb[2] = {0, 0};
	txb[0] = (uint8_t)((tx >> 8) & 0xFF);
	txb[1] = (uint8_t)(tx & 0xFF);

	HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(drv->hspi, txb, rxb, 2, DRV8323_SPI_TIMEOUT_MS);
	if (st != HAL_OK) {
		return st;
	}
	if (rx) {
		*rx = ((uint16_t)rxb[0] << 8) | (uint16_t)rxb[1];
	}
	return HAL_OK;
}

HAL_StatusTypeDef drv8323_write_reg(drv8323_t *drv, uint8_t addr, uint16_t value)
{
	uint16_t frame = ((uint16_t)(addr & DRV8323_SPI_ADDR_MASK) << DRV8323_SPI_ADDR_SHIFT)
				   | (value & DRV8323_SPI_DATA_MASK);
	return drv8323_spi_transfer(drv, frame, NULL);
}

HAL_StatusTypeDef drv8323_read_reg(drv8323_t *drv, uint8_t addr, uint16_t *value)
{
	uint16_t rx = 0;
	uint16_t frame = DRV8323_SPI_RW_READ
				   | ((uint16_t)(addr & DRV8323_SPI_ADDR_MASK) << DRV8323_SPI_ADDR_SHIFT);
	HAL_StatusTypeDef st = drv8323_spi_transfer(drv, frame, &rx);
	if (st != HAL_OK) return st;
	if (value) {
		*value = (rx & DRV8323_SPI_DATA_MASK);
	}
	return HAL_OK;
}

// ----------------------------------------------
// Public API implementation
// ----------------------------------------------
void drv8323_init(drv8323_t *drv,
				  SPI_HandleTypeDef *hspi,
				  GPIO_TypeDef *enable_port, uint16_t enable_pin,
				  GPIO_TypeDef *hiz_port,    uint16_t hiz_pin,
				  GPIO_TypeDef *fault_port,  uint16_t fault_pin,
				  TIM_HandleTypeDef *htim_pwm,
				  uint32_t tim_channel_u,
				  uint32_t tim_channel_v,
				  uint32_t tim_channel_w)
{
	if (!drv) return;

	memset(drv, 0, sizeof(*drv));
	drv->hspi = hspi;
	drv->enable_port = enable_port;
	drv->enable_pin = enable_pin;
	drv->hiz_port = hiz_port;
	drv->hiz_pin = hiz_pin;
	drv->fault_port = fault_port;
	drv->fault_pin = fault_pin;
	drv->htim_pwm = htim_pwm;
	drv->tim_channel_u = tim_channel_u;
	drv->tim_channel_v = tim_channel_v;
	drv->tim_channel_w = tim_channel_w;

	// Safe startup state
	drv->duty_u = 0.0f;
	drv->duty_v = 0.0f;
	drv->duty_w = 0.0f;

	// Keep driver asleep
	if (drv->enable_port) {
		HAL_GPIO_WritePin(drv->enable_port, drv->enable_pin, GPIO_PIN_RESET);
	}
	// Put inverter in Hi-Z (coast)
	if (drv->hiz_port) {
		// hiz=true => MOTOR_HIZ low (per project wiring: 0 => Hi-Z)
		HAL_GPIO_WritePin(drv->hiz_port, drv->hiz_pin, GPIO_PIN_RESET);
	}

	drv->initialized = true;
}

HAL_StatusTypeDef drv8323_clear_faults(drv8323_t *drv)
{
	if (!drv) return HAL_ERROR;

	// Try CLR_FLT write pulse
	HAL_StatusTypeDef st = drv8323_write_reg(drv, DRV8323_REG_DRV_CTRL, DRV8323_DRVCTRL_CLR_FLT);
	if (st != HAL_OK) return st;
	// Optionally clear back to 0 to avoid holding the bit
	st = drv8323_write_reg(drv, DRV8323_REG_DRV_CTRL, 0u);
	if (st != HAL_OK) return st;

	// If external nFAULT is still low, fallback: brief nSLEEP cycle
	if (drv->fault_port) {
		GPIO_PinState s = HAL_GPIO_ReadPin(drv->fault_port, drv->fault_pin);
		if (s == GPIO_PIN_RESET) {
			if (drv->enable_port) {
				HAL_GPIO_WritePin(drv->enable_port, drv->enable_pin, GPIO_PIN_RESET);
				HAL_Delay(2);
				HAL_GPIO_WritePin(drv->enable_port, drv->enable_pin, GPIO_PIN_SET);
				HAL_Delay(2);
			}
		}
	}
	return HAL_OK;
}

HAL_StatusTypeDef drv8323_read_faults(drv8323_t *drv, uint16_t *fault_mask)
{
	if (!drv || !fault_mask) return HAL_ERROR;

	uint16_t fs1 = 0, fs2 = 0;
	HAL_StatusTypeDef st = drv8323_read_reg(drv, DRV8323_REG_FAULT_STATUS_1, &fs1);
	if (st != HAL_OK) return st;
	st = drv8323_read_reg(drv, DRV8323_REG_VGS_STATUS_2, &fs2);
	if (st != HAL_OK) return st;

	uint16_t m = DRV8323_FAULT_NONE;

	// FAULT_STATUS_1 mapping (verify bit positions per datasheet)
	// Bits 0..5: VDS_Lx / VDS_Hx => any indicates VDS overcurrent
	if (fs1 & 0x003Fu) {
		m |= DRV8323_FAULT_VDS_OCP;
	}
	// Bit 6: OTSD
	if (fs1 & (1u << 6)) {
		m |= DRV8323_FAULT_OTSD;
	}
	// Bit 7: UVLO
	if (fs1 & (1u << 7)) {
		m |= DRV8323_FAULT_UVLO;
	}
	// Bit 8: GDF
	if (fs1 & (1u << 8)) {
		m |= DRV8323_FAULT_GDF;
	}

	// VGS_STATUS_2 mapping (verify bit positions per datasheet)
	// Assume 0,2,4 => VGS_LS faults; 1,3,5 => VGS_HS faults; 6 => CPUV
	uint16_t ls_mask = (1u << 0) | (1u << 2) | (1u << 4);
	uint16_t hs_mask = (1u << 1) | (1u << 3) | (1u << 5);
	if (fs2 & ls_mask) m |= DRV8323_FAULT_VGS_LS;
	if (fs2 & hs_mask) m |= DRV8323_FAULT_VGS_HS;
	if (fs2 & (1u << 6)) m |= DRV8323_FAULT_CPUV;

	// Any other overcurrent related flags in these registers
	if ((fs1 | fs2) & 0x0200u) m |= DRV8323_FAULT_OC_FAULTS;

	*fault_mask = m;
	return HAL_OK;
}

void drv8323_enable(drv8323_t *drv)
{
	if (!drv) return;
	if (drv->enable_port) {
		HAL_GPIO_WritePin(drv->enable_port, drv->enable_pin, GPIO_PIN_SET);
		// Allow charge pump and internal references to stabilize
		HAL_Delay(2);
	}
}

void drv8323_disable(drv8323_t *drv)
{
	if (!drv) return;

	// Force Hi-Z for safety before disabling
	drv8323_set_hiz(drv, true);

	if (drv->enable_port) {
		HAL_GPIO_WritePin(drv->enable_port, drv->enable_pin, GPIO_PIN_RESET);
	}
}

void drv8323_set_hiz(drv8323_t *drv, bool hiz)
{
	if (!drv) return;
	if (!drv->hiz_port) return;

	// Project wiring definition:
	// MOTOR_HIZ = 1 => INLA/INLB/INLC = 1 (phases active)
	// MOTOR_HIZ = 0 => INLA/INLB/INLC = 0 (all phases Hi-Z)
	if (hiz) {
		HAL_GPIO_WritePin(drv->hiz_port, drv->hiz_pin, GPIO_PIN_RESET);
	} else {
		HAL_GPIO_WritePin(drv->hiz_port, drv->hiz_pin, GPIO_PIN_SET);
	}
}

void drv8323_set_duty_uvw(drv8323_t *drv, float u, float v, float w)
{
	if (!drv || !drv->htim_pwm) return;

	// Clamp to [-1, 1]
	u = drv8323_clampf(u, -1.0f, 1.0f);
	v = drv8323_clampf(v, -1.0f, 1.0f);
	w = drv8323_clampf(w, -1.0f, 1.0f);

	// Map to [0,1]: -1 => 0, 0 => 0.5, +1 => 1
	float du = 0.5f * (u + 1.0f);
	float dv = 0.5f * (v + 1.0f);
	float dw = 0.5f * (w + 1.0f);

	uint32_t arr = drv8323_get_tim_arr(drv->htim_pwm);
	uint32_t ccr_u = (uint32_t)(du * (float)arr);
	uint32_t ccr_v = (uint32_t)(dv * (float)arr);
	uint32_t ccr_w = (uint32_t)(dw * (float)arr);

	__HAL_TIM_SET_COMPARE(drv->htim_pwm, drv->tim_channel_u, ccr_u);
	__HAL_TIM_SET_COMPARE(drv->htim_pwm, drv->tim_channel_v, ccr_v);
	__HAL_TIM_SET_COMPARE(drv->htim_pwm, drv->tim_channel_w, ccr_w);

	drv->duty_u = u;
	drv->duty_v = v;
	drv->duty_w = w;

	// Note on theory:
	// The U/V/W values here are typically the outputs of a higher-level FOC loop
	// or SPWM generator. The timer should run center-aligned mode so the compare
	// acts like comparing sinusoidal references against a triangular carrier. The
	// DRV8323 in 3-PWM mode uses INHx PWM to drive the phase high/low with internal
	// deadtime, while INLx (tied together via MOTOR_HIZ in this design) globally
	// enables/disables the three phases.
}

HAL_StatusTypeDef drv8323_configure_default(drv8323_t *drv)
{
	if (!drv) return HAL_ERROR;

	// Ensure device is enabled
	drv8323_enable(drv);
	HAL_Delay(2);

	HAL_StatusTypeDef st;

	// Clear any existing faults
	st = drv8323_clear_faults(drv);
	if (st != HAL_OK) return st;

	// Configure DRV_CTRL: 3x PWM mode, report OTW, enable GDF (not disabled)
	// Note: Field positions vary; adjust with datasheet if needed.
	uint16_t drv_ctrl = 0u
					  | DRV8323_DRVCTRL_OTW_REP
					  | DRV8323_DRVCTRL_PWM_MODE_3;
	st = drv8323_write_reg(drv, DRV8323_REG_DRV_CTRL, drv_ctrl);
	if (st != HAL_OK) return st;

	// Configure GATE_DRV_HS: moderate IDRIVE settings
	// Example packing: [LOCK(2:0)][IDRIVEP_HS(3:0)][IDRIVEN_HS(3:0)] -> 11 bits total
	// LOCK=0b011 (unlock), IDRIVEP_HS=0x5, IDRIVEN_HS=0x6
	uint16_t gate_hs = (0x3u << 8) | ((DRV8323_IDRIVEP_50mA & 0xFu) << 4) | (DRV8323_IDRIVEN_100mA & 0xFu);
	st = drv8323_write_reg(drv, DRV8323_REG_GATE_DRV_HS, gate_hs);
	if (st != HAL_OK) return st;

	// Configure GATE_DRV_LS: TDRIVE=500ns and similar drive strengths
	// Example packing: [CBC(1)][TDRIVE(1:0)][IDRIVEP_LS(3:0)][IDRIVEN_LS(3:0)]
	uint16_t gate_ls = (0u << 10) | ((DRV8323_TDRIVE_500ns & 0x3u) << 8)
					 | ((DRV8323_IDRIVEP_50mA & 0xFu) << 4) | (DRV8323_IDRIVEN_100mA & 0xFu);
	st = drv8323_write_reg(drv, DRV8323_REG_GATE_DRV_LS, gate_ls);
	if (st != HAL_OK) return st;

	// Configure OCP control: mid VDS threshold, latch on OC, deglitch ~4us
	// Example packing: [TRETRY][DEADTIME][OCP_DEG(1:0)][OCP_MODE(1:0)][VDS_LVL(4:0)]
	uint16_t ocp = ((DRV8323_OCP_DEG_4us & 0x3u) << 8)
				 | ((DRV8323_OCP_MODE_LATCH & 0x3u) << 6)
				 | (DRV8323_VDS_LVL_MID & 0x1Fu);
	st = drv8323_write_reg(drv, DRV8323_REG_OCP_CTRL, ocp);
	if (st != HAL_OK) return st;

	// Configure CSA: gain=20V/V, reference 0.25V, sense on low side, no calibration
	// Example packing: [CSA_FET][VREF_DIV][LS_REF][VREF][CSA_GAIN(1:0)][CSA_CAL(1:0)]
	uint16_t csa = ((DRV8323_CSA_FET_LOW_SIDE & 0x1u) << 10)
				 | ((DRV8323_CSA_VREF_0V25 & 0x3u) << 6)
				 | ((DRV8323_CSA_GAIN_20V & 0x3u) << 2)
				 | (DRV8323_CSA_CAL_CH1_OFF & 0x3u);
	st = drv8323_write_reg(drv, DRV8323_REG_CSA_CTRL, csa);
	if (st != HAL_OK) return st;

	return HAL_OK;
}

