/**
  ******************************************************************************
  * @file    cli.h
  * @brief   串口命令行模块：USART1 收发 + 命令解析 + printf 重定向。
  ******************************************************************************
  */
#ifndef __CLI_H__
#define __CLI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 初始化 USART1（PA9=TX, PA10=RX, 115200 8N1，开接收中断） */
void CLI_Init(void);

/* 由 stm32f1xx_it.c 的 USART1_IRQHandler() 调用 */
void CLI_IRQHandler(void);

/* 主循环调用：解析并执行收到的命令 */
void CLI_Task(void);

/* 阻塞式发送一个字符串（供中断外使用） */
void CLI_Puts(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* __CLI_H__ */
