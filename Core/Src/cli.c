/**
  ******************************************************************************
  * @file    cli.c
  * @brief   串口命令行：USART1 寄存器级收发 + 简单命令解析 + printf 重定向。
  *
  *  本工程的 HAL 库未包含 UART 驱动文件（Drivers 目录里没有 hal_uart.c），
  *  因此 USART1 采用寄存器直写，不依赖 HAL_UART 模块。
  *
  *  命令：
  *    f <hz>      设置 PWM 输出频率（Hz），例如 f 1000
  *    d <pct>     设置占空比（%），例如 d 30
  *    r <psc>     设置输入捕获量程（预分频），0=72MHz，71=1MHz
  *    start/stop  启动 / 停止 PWM 输出
  *    ?           打印一次三方对照信息（设定值 / 捕获值）
  *    help        命令列表
  ******************************************************************************
  */

#include <stdio.h>
#include "cli.h"
#include "pwm_gen.h"
#include "pwm_cap.h"

#define CLI_RX_BUF_SIZE   64U
#define CLI_BAUD          115200U

static char              s_rx_buf[CLI_RX_BUF_SIZE];
static volatile uint16_t s_rx_len   = 0U;
static volatile uint8_t  s_rx_ready = 0U;

/* ======================================================= 底层发送 */
static void uart_putc(char c)
{
  while ((USART1->SR & USART_SR_TXE) == 0U)
  {
    /* 等待发送数据寄存器空 */
  }
  USART1->DR = (uint16_t)((uint8_t)c);
}

void CLI_Puts(const char *s)
{
  while (*s != '\0')
  {
    uart_putc(*s++);
  }
}

/* printf 重定向：Keil 工程需勾选 "Use MicroLIB" */
int fputc(int ch, FILE *f)
{
  (void)f;
  uart_putc((char)ch);
  return ch;
}

/* ======================================================= 初始化 */
void CLI_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  uint32_t         pclk2;

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_USART1_CLK_ENABLE();

  /* PA9 = USART1_TX（复用推挽） */
  gpio.Pin   = GPIO_PIN_9;
  gpio.Mode  = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio);

  /* PA10 = USART1_RX（浮空输入） */
  gpio.Pin  = GPIO_PIN_10;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio);

  /* 波特率：BRR = USARTDIV * 16 = fPCLK2 / baud（四舍五入） */
  pclk2 = HAL_RCC_GetPCLK2Freq();
  USART1->BRR = (uint16_t)((pclk2 + (CLI_BAUD / 2U)) / CLI_BAUD);

  /* 8 位数据、1 位停止、无校验 */
  USART1->CR2 = 0U;
  USART1->CR3 = 0U;
  USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

  HAL_NVIC_SetPriority(USART1_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/* ======================================================= 接收中断 */
void CLI_IRQHandler(void)
{
  if ((USART1->SR & USART_SR_RXNE) != 0U)
  {
    char c = (char)(USART1->DR & 0xFFU);    /* 读 DR 自动清 RXNE */

    if ((c == '\r') || (c == '\n'))
    {
      if (s_rx_len > 0U)
      {
        s_rx_buf[s_rx_len] = '\0';
        s_rx_ready = 1U;
      }
    }
    else if (s_rx_len < (CLI_RX_BUF_SIZE - 1U))
    {
      s_rx_buf[s_rx_len++] = c;
    }
  }
  else
  {
    (void)USART1->DR;                       /* 清除 ORE 等错误标志 */
  }
}

/* ======================================================= 解析辅助 */
static const char *skip_blank(const char *s)
{
  while ((*s == ' ') || (*s == '\t'))
  {
    s++;
  }
  return s;
}

static int starts_with(const char *s, const char *prefix)
{
  while (*prefix != '\0')
  {
    if (*s++ != *prefix++)
    {
      return 0;
    }
  }
  return 1;
}

static int parse_uint(const char *s, uint32_t *v)
{
  uint32_t    r = 0U;
  const char *p = skip_blank(s);

  if ((*p < '0') || (*p > '9'))
  {
    return 0;
  }
  while ((*p >= '0') && (*p <= '9'))
  {
    r = (r * 10U) + (uint32_t)(*p - '0');
    p++;
  }
  *v = r;
  return 1;
}

static int parse_float(const char *s, float *v)
{
  float       r   = 0.0f;
  float       f   = 0.1f;
  int         any = 0;
  const char *p   = skip_blank(s);

  if ((*p < '0') || (*p > '9'))
  {
    return 0;
  }
  while ((*p >= '0') && (*p <= '9'))
  {
    r = (r * 10.0f) + (float)(*p - '0');
    p++;
    any = 1;
  }
  if (*p == '.')
  {
    p++;
    while ((*p >= '0') && (*p <= '9'))
    {
      r += (float)(*p - '0') * f;
      f *= 0.1f;
      p++;
      any = 1;
    }
  }
  if (any == 0)
  {
    return 0;
  }
  *v = r;
  return 1;
}

/* ======================================================= 报告 */
static void cli_report(void)
{
  float cf = 0.0f, cd = 0.0f, cw = 0.0f;
  int   ok = (PWM_Cap_Get(&cf, &cd, &cw) == 0);

  printf("SET: f=%.2fHz d=%.2f%%\r\n",
         (double)PWM_GetFreq(), (double)PWM_GetDuty());

  if (ok != 0)
  {
    printf("CAP: f=%.2fHz d=%.2f%% T=%.2fus W=%.2fus\r\n",
           (double)cf, (double)cd, (double)(1000000.0f / cf), (double)cw);
  }
  else
  {
    CLI_Puts("CAP: no signal (check PA6 input)\r\n");
  }
}

static void cli_help(void)
{
  CLI_Puts(
    "commands:\r\n"
    "  f <hz>     set PWM frequency (e.g. f 1000)\r\n"
    "  d <pct>    set PWM duty %    (e.g. d 30)\r\n"
    "  r <psc>    capture prescaler (0=72MHz, 71=1MHz)\r\n"
    "  start      start PWM output\r\n"
    "  stop       stop  PWM output\r\n"
    "  ?          print SET vs CAP\r\n"
    "  help       this list\r\n");
}

/* ======================================================= 命令处理 */
void CLI_Task(void)
{
  char        line[CLI_RX_BUF_SIZE];
  const char *p;
  uint16_t    i;
  uint16_t    n;

  if (s_rx_ready == 0U)
  {
    return;
  }

  n = s_rx_len;
  for (i = 0U; i < n; i++)
  {
    line[i] = s_rx_buf[i];
  }
  line[n] = '\0';
  s_rx_len   = 0U;
  s_rx_ready = 0U;

  p = skip_blank(line);
  if (*p == '\0')
  {
    return;
  }

  if (*p == '?')
  {
    cli_report();
    return;
  }

  if (starts_with(p, "start"))
  {
    PWM_Gen_Start();
    CLI_Puts("PWM started\r\n");
    return;
  }

  if (starts_with(p, "stop"))
  {
    PWM_Gen_Stop();
    CLI_Puts("PWM stopped\r\n");
    return;
  }

  if (starts_with(p, "help"))
  {
    cli_help();
    return;
  }

  if ((*p == 'f') || (*p == 'F'))
  {
    uint32_t hz;
    if ((parse_uint(p + 1, &hz) != 0) && (PWM_SetFreq(hz) == 0))
    {
      cli_report();
    }
    else
    {
      CLI_Puts("ERR: usage: f <hz>  (e.g. f 1000)\r\n");
    }
    return;
  }

  if ((*p == 'd') || (*p == 'D'))
  {
    float pct;
    if ((parse_float(p + 1, &pct) != 0) && (PWM_SetDuty(pct) == 0))
    {
      cli_report();
    }
    else
    {
      CLI_Puts("ERR: usage: d <percent>  (e.g. d 30)\r\n");
    }
    return;
  }

  if ((*p == 'r') || (*p == 'R'))
  {
    uint32_t psc;
    if (parse_uint(p + 1, &psc) != 0)
    {
      PWM_Cap_SetPrescaler(psc);
      CLI_Puts("capture prescaler updated\r\n");
    }
    else
    {
      CLI_Puts("ERR: usage: r <psc>  (0=72MHz, 71=1MHz)\r\n");
    }
    return;
  }

  CLI_Puts("ERR: unknown command, type 'help'\r\n");
}
