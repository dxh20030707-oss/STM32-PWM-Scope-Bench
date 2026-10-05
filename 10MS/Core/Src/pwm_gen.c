/**
  ******************************************************************************
  * @file    pwm_gen.c
  * @brief   PWM 信号源模块：TIM1_CH1 (PA8) 输出频率/占空比可调的 PWM。
  *
  *  原理：
  *    TIM1 挂在 APB2(72MHz)，因 APB2 预分频 = 1，故 TIM1 计数时钟 = 72MHz。
  *    CK_CNT   = 72MHz / (PSC + 1)
  *    输出频率 = CK_CNT / (ARR + 1)
  *    占空比   = CCR / (ARR + 1)
  *    为使 ARR 落在 16 位范围，按目标频率自动选择最小的 PSC。
  ******************************************************************************
  */

#include "pwm_gen.h"

/* TIM1 计数时钟（Hz） */
#define PWM_TIM_CLK_HZ   72000000UL

TIM_HandleTypeDef htim1;

/* 目标设定值 */
static uint32_t s_target_freq = 1000U;
static float    s_target_duty = 50.0f;

/* 实际输出值（供串口查询与三方对照使用） */
static float s_actual_freq = 1000.0f;
static float s_actual_duty = 50.0f;

/* ----------------------------------------------------------------------------
 * 初始化 TIM1 为 PWM 输出模式（PA8 = TIM1_CH1）
 * -------------------------------------------------------------------------- */
void PWM_Gen_Init(void)
{
  GPIO_InitTypeDef   gpio = {0};
  TIM_OC_InitTypeDef oc   = {0};

  /* 1. 开时钟 */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM1_CLK_ENABLE();

  /* 2. PA8 复用推挽输出（Speed = HIGH，保证方波边沿陡峭） */
  gpio.Pin   = GPIO_PIN_8;
  gpio.Mode  = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio);

  /* 3. TIM1 时基（PSC/ARR 随后由 PWM_SetFreqDuty 覆盖） */
  htim1.Instance               = TIM1;
  htim1.Init.Prescaler         = 71U;
  htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim1.Init.Period            = 999U;
  htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0U;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }

  /* 4. CH1 输出比较：PWM 模式 1（CNT < CCR 输出高电平） */
  oc.OCMode       = TIM_OCMODE_PWM1;
  oc.Pulse        = 500U;
  oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
  oc.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
  oc.OCFastMode   = TIM_OCFAST_DISABLE;
  oc.OCIdleState  = TIM_OCIDLESTATE_RESET;
  oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  /* 5. 应用默认参数并启动输出 */
  (void)PWM_SetFreqDuty(1000U, 50.0f);
  PWM_Gen_Start();
}

/* ----------------------------------------------------------------------------
 * 设置频率与占空比
 * -------------------------------------------------------------------------- */
int PWM_SetFreqDuty(uint32_t freq_hz, float duty_percent)
{
  uint32_t psc;
  uint32_t arr;   /* ARR + 1，即一个周期的计数个数 */
  uint32_t ccr;
  uint32_t ticks;

  if (freq_hz == 0U)
  {
    return -1;
  }

  if (duty_percent < 0.0f)   { duty_percent = 0.0f;   }
  if (duty_percent > 100.0f) { duty_percent = 100.0f; }

  /* 无预分频时一个周期需要的计数 */
  ticks = PWM_TIM_CLK_HZ / freq_hz;
  if (ticks == 0U)
  {
    return -1;                       /* 频率高于 72MHz/2，不可达 */
  }

  if (ticks <= 65536UL)
  {
    psc = 0U;
    arr = ticks;
  }
  else
  {
    /* 选择最小 PSC，使 ARR 落在 16 位范围内 */
    psc = ((ticks + 65535UL) / 65536UL) - 1UL;
    if (psc > 0xFFFFUL)
    {
      return -1;                     /* 频率过低，超出 PSC 能力 */
    }
    arr = (PWM_TIM_CLK_HZ / (psc + 1UL)) / freq_hz;
    if ((arr == 0U) || (arr > 65536UL))
    {
      return -1;
    }
  }

  /* 计算比较值（四舍五入） */
  ccr = (uint32_t)(((float)arr * duty_percent / 100.0f) + 0.5f);
  if (ccr > arr)      { ccr = arr;      }
  if (ccr > 0xFFFFUL) { ccr = 0xFFFFUL; }

  /* 写入寄存器（ARR 寄存器实际写入 arr-1） */
  __HAL_TIM_SET_PRESCALER(&htim1, psc);
  __HAL_TIM_SET_AUTORELOAD(&htim1, arr - 1UL);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr);

  /* 产生更新事件，让 PSC/ARR 立即生效 */
  htim1.Instance->EGR = TIM_EGR_UG;

  /* 记录目标值与实际输出值（整除带来的量化已体现在 ARR/CCR 中） */
  s_target_freq = freq_hz;
  s_target_duty = duty_percent;
  s_actual_freq = (float)(PWM_TIM_CLK_HZ / (psc + 1UL)) / (float)arr;
  s_actual_duty = (float)ccr * 100.0f / (float)arr;

  return 0;
}

int PWM_SetFreq(uint32_t freq_hz)
{
  return PWM_SetFreqDuty(freq_hz, s_target_duty);
}

int PWM_SetDuty(float duty_percent)
{
  return PWM_SetFreqDuty(s_target_freq, duty_percent);
}

void PWM_Gen_Start(void)
{
  (void)HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
}

void PWM_Gen_Stop(void)
{
  (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
}

float PWM_GetFreq(void)
{
  return s_actual_freq;
}

float PWM_GetDuty(void)
{
  return s_actual_duty;
}
