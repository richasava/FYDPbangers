/**
  ******************************************************************************
  * @file    wcl.h
  * @brief   Wireless Communication Layer - BAU (STM32) side.
  *
  * Owns USART1 (PA9 TX / PA10 RX @ 921600 baud), the UART bridge to the ESP32.
  * Encodes biometric samples as protobuf (nanopb), COBS-frames them, and streams
  * them to the ESP32, which republishes over MQTT to the host broker.
  *
  * Debug printf stays on USART2 (ST-Link VCP); this module never touches it.
  ******************************************************************************
  */
#ifndef WCL_H
#define WCL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure USART1 (PA9/PA10) and internal WCL state.
 *        Call once after HAL_Init() / SystemClock_Config().
 * @return true on success.
 */
bool WCL_Init(void);

/**
 * @brief Encode one biometric frame and transmit it to the ESP32.
 *
 * Channels that are not yet implemented should be passed with their *_valid
 * flag false; the AGE will ignore them. During BAU bring-up only motion is
 * live, so call e.g. WCL_SendBiometricFrame(roll, pitch, yaw, true, 0, false, 0, false).
 *
 * @param roll,pitch,yaw  Euler angles in degrees.
 * @param imu_valid       true once IMU fusion is producing usable angles.
 * @param hr_bpm          heart rate; ignored unless hr_valid.
 * @param hr_valid        true once the PPG channel is live.
 * @param gsr             normalized skin conductance; ignored unless gsr_valid.
 * @param gsr_valid       true once the GSR channel is live.
 * @return true if the frame was encoded and handed to the UART.
 */
bool WCL_SendBiometricFrame(float roll, float pitch, float yaw, bool imu_valid,
                            float hr_bpm, bool hr_valid,
                            float gsr, bool gsr_valid);

#ifdef __cplusplus
}
#endif

#endif /* WCL_H */
