/**
  ******************************************************************************
  * @file    pwm_gen.h
  * @brief   PWM 信号源模块：TIM1_CH1 (PA8) 输出频率/占空比可调的 PWM。
  ******************************************************************************
  */
#ifndef __PWM_GEN_H__
#define __PWM_GEN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern TIM_HandleTypeDef htim1;

/* 初始化 TIM1 PWM 输出（PA8），并按默认参数启动 */
void PWM_Gen_Init(void);

/* 设置输出频率(Hz) 和占空比(0.0 ~ 100.0 %)
 * 返回 0 成功；-1 表示频率超出可达范围（约 0.017Hz ~ 36MHz） */
int PWM_SetFreqDuty(uint32_t freq_hz, float duty_percent);

/* 只改频率（保持当前占空比设定）/ 只改占空比（保持当前频率设定） */
int PWM_SetFreq(uint32_t freq_hz);
int PWM_SetDuty(float duty_percent);

/* 启动 / 停止 PWM 输出 */
void PWM_Gen_Start(void);
void PWM_Gen_Stop(void);

/* 读取实际输出值。
 * 注意：受 16 位 ARR/CCR 量化影响，高频时可能与设定值略有偏差，
 *       这两个函数返回的才是"芯片真正输出"的值，可用于三方对照。 */
float PWM_GetFreq(void);
float PWM_GetDuty(void);

#ifdef __cplusplus
}
#endif

#endif /* __PWM_GEN_H__ */
