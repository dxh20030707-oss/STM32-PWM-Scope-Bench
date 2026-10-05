/**
  ******************************************************************************
  * @file    pwm_cap.h
  * @brief   PWM 输入捕获模块：TIM3_CH1 (PA6) 测量外部 PWM 的频率/占空比/脉宽。
  ******************************************************************************
  */
#ifndef __PWM_CAP_H__
#define __PWM_CAP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern TIM_HandleTypeDef htim3;

/* 初始化 TIM3 为 PWM 输入捕获模式（PA6 = TIM3_CH1） */
void PWM_Cap_Init(void);

/* 切换量程：预分频 psc，计数频率 = 72MHz/(psc+1)
 * psc=71 -> 1MHz 计数，测低频能力较强；psc=0 -> 72MHz，测高频能力较强 */
void PWM_Cap_SetPrescaler(uint32_t psc);

/* 读取一次测量结果（任意形参可传 NULL）
 * 返回 0 成功；-1 表示无信号或数据未就绪 */
int PWM_Cap_Get(float *freq_hz, float *duty_pct, float *pulse_us);

#ifdef __cplusplus
}
#endif

#endif /* __PWM_CAP_H__ */
