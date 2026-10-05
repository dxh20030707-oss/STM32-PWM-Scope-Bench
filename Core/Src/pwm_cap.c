/**
  ******************************************************************************
  * @file    pwm_cap.c
  * @brief   PWM 输入捕获模块：TIM3_CH1 (PA6) 用 PWM Input 模式测量
  *          外部 PWM 的频率 / 占空比 / 脉宽。
  *
  *  原理（PWM Input 模式）：
  *    同一个 TI1 输入：
  *      CH1 捕获上升沿，CH2 捕获下降沿；
  *      上升沿经从模式(Reset)复位计数器。
  *    于是  CCR1 = 一个周期的计数个数，CCR2 = 高电平的计数个数。
  *
  *      TIM3 挂 APB1(36MHz)，但 APB1 预分频 != 1，定时器时钟被 x2 = 72MHz。
  *      计数频率 = 72MHz / (PSC + 1)
  *      频率     = 计数频率 / CCR1
  *      占空比   = CCR2 / CCR1
  *      脉宽     = CCR2 / 计数频率
  *
  *  注意：从模式复位法要求"一个周期内的计数值 < 65536"（否则会溢出丢周期），
  *        低频测量需要加大 PSC —— 用 PWM_Cap_SetPrescaler() 切换量程。
  ******************************************************************************
  */

#include <stddef.h>
#include "pwm_cap.h"

/* TIM3 计数时钟（Hz）：72MHz */
#define PWM_CAP_TIM_CLK_HZ   72000000UL

/* 默认预分频：PSC=71 -> 1MHz 计数（1us 分辨率），可测下限约 15Hz */
#define PWM_CAP_PSC_DEFAULT  71U

TIM_HandleTypeDef htim3;

static uint32_t s_psc = PWM_CAP_PSC_DEFAULT;

/* ----------------------------------------------------------------------------
 * 初始化 TIM3 为 PWM 输入捕获模式（PA6 = TIM3_CH1）
 * -------------------------------------------------------------------------- */
/**
 * @brief PWM输入捕获初始化函数
 * 该函数配置TIM3的输入捕获功能，用于测量PWM信号的周期和占空比
 */
void PWM_Cap_Init(void)  //
{
  GPIO_InitTypeDef   gpio = {0};    // GPIO初始化结构体变量
  TIM_IC_InitTypeDef ic   = {0};    // 定时器输入捕获初始化结构体变量

  /* 1. 开时钟 */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();

  /* 2. PA6 输入，下拉（悬空时保持低电平，避免噪声误触发） */
  gpio.Pin  = GPIO_PIN_6;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &gpio);

  /* 3. 时基：自动重装拉满，配合从模式复位实现周期测量 */
  htim3.Instance               = TIM3;
  htim3.Init.Prescaler         = s_psc;
  htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim3.Init.Period            = 0xFFFFU;
  htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_IC_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }

  /* 4. CH1：上升沿，直接映射到 TI1 */
  ic.ICPolarity  = TIM_INPUTCHANNELPOLARITY_RISING;
  ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
  ic.ICPrescaler = TIM_ICPSC_DIV1;
  ic.ICFilter    = 0U;
  if (HAL_TIM_IC_ConfigChannel(&htim3, &ic, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  /* 5. CH2：下降沿，间接映射到 TI1（同一个引脚） */
  ic.ICPolarity  = TIM_INPUTCHANNELPOLARITY_FALLING;
  ic.ICSelection = TIM_ICSELECTION_INDIRECTTI;
  if (HAL_TIM_IC_ConfigChannel(&htim3, &ic, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }

  /* 6. 从模式：触发源 = TI1FP1，复位模式（每个上升沿把 CNT 清零） */
  TIM3->SMCR = TIM_SMCR_TS_2 | TIM_SMCR_TS_0 | TIM_SMCR_SMS_2;

  /* 7. 启动两路输入捕获（主循环轮询 CCR，不需要中断） */
  (void)HAL_TIM_IC_Start(&htim3, TIM_CHANNEL_1);
  (void)HAL_TIM_IC_Start(&htim3, TIM_CHANNEL_2);
}

/* ----------------------------------------------------------------------------
 * 切换量程
 * -------------------------------------------------------------------------- */
void PWM_Cap_SetPrescaler(uint32_t psc)
{
  if (psc > 0xFFFFU)
  {
    return;
  }
  s_psc = psc;
  TIM3->PSC = (uint16_t)psc;    /* 在下一个更新事件生效 */
}

/* ----------------------------------------------------------------------------
 * 读取测量结果
 * -------------------------------------------------------------------------- */
int PWM_Cap_Get(float *freq_hz, float *duty_pct, float *pulse_us)
{
  uint32_t ccr1, ccr2, ccr1b, ccr2b;
  float    clk;

  /* 连读两次，确保拿到完整且一致的一对捕获值（避免读到更新中的一半） */
  ccr1  = TIM3->CCR1; ccr2  = TIM3->CCR2;
  ccr1b = TIM3->CCR1; ccr2b = TIM3->CCR2;
  if ((ccr1 != ccr1b) || (ccr2 != ccr2b))
  {
    return -1;                 /* 正在被硬件更新，稍后再读 */
  }
  if (ccr1 == 0U)
  {
    return -1;                 /* 无信号 */
  }
  if (ccr2 > ccr1)
  {
    ccr2 = ccr1;               /* 噪声导致的异常值，钳位到 100% */
  }

  clk = (float)(PWM_CAP_TIM_CLK_HZ / (s_psc + 1UL));

  if (freq_hz  != NULL) { *freq_hz  = clk / (float)ccr1; }
  if (duty_pct != NULL) { *duty_pct = (float)ccr2 * 100.0f / (float)ccr1; }
  if (pulse_us != NULL) { *pulse_us = (float)ccr2 * 1000000.0f / clk; }

  return 0;
}
