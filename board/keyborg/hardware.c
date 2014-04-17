/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Hardware initialization and common functions */

#include "common.h"
#include "cpu.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

static void clock_init(void)
{
	/* Ensure that HSI is ON */
	if (!(STM32_RCC_CR & (1 << 1))) {
		/* Enable HSI */
		STM32_RCC_CR |= 1 << 0;
		/* Wait for HSI to be ready */
		while (!(STM32_RCC_CR & (1 << 1)))
			;
	}

	/* PLLSRC = HSI/2, PLLMUL = x12 (x HSI/2) = 48MHz */
	STM32_RCC_CFGR = 0x00684000;
	/* Enable PLL */
	STM32_RCC_CR |= 1 << 24;
	/* Wait for PLL to be ready */
	while (!(STM32_RCC_CR & (1 << 25)))
			;

	/* switch SYSCLK to PLL */
	STM32_RCC_CFGR = 0x00684002;
	/* wait until the PLL is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0x8)
		;
}

static void power_init(void)
{
	/* enable ADC1, ADC2, PMSE, SPI1, GPA-GPI, AFIO */
	STM32_RCC_APB2ENR = 0x0000f7fd;
	/* enable TIM2, TIM3, PWR */
	STM32_RCC_APB1ENR = 0x10000003;
	/* enable DMA, SRAM */
	STM32_RCC_AHBENR  = 0x000005;
}

/* GPIO setting helpers */
#define OUT(n)    (0x1 << ((n & 0x7) * 4))
#define OUT50(n)  (0x3 << ((n & 0x7) * 4))
#define ANALOG(n) (0x0)
#define FLOAT(n)  (0x4 << ((n & 0x7) * 4))
#define GP_PP(n)  (0x0)
#define GP_OD(n)  (0x4 << ((n & 0x7) * 4))
#define AF_PP(n)  (0x8 << ((n & 0x7) * 4))
#define AF_OD(n)  (0xc << ((n & 0x7) * 4))

static void pins_init(void)
{
	/* Enable SWD, but disable JTAG. We want JTDI as GPIO. */
	STM32_GPIO_AFIO_MAPR = (STM32_GPIO_AFIO_MAPR & ~(0x7 << 24))
			       | (2 << 24);

	/* Pin usage:
	 * PA15: UART TX - OUTPUT/HIGH
	 */
	STM32_GPIO_CRH(GPIO_A) = OUT(15) | GP_PP(15);
}

static void adc_init(void)
{
	int id;

	for (id = 0; id < 2; ++id) {
		/* Enable ADC clock */
		STM32_RCC_APB2ENR |= (1 << (14 + id));

		/* Power on ADC if it's off */
		if (!(STM32_ADC_CR2(id) & (1 << 0))) {
			/* Power on ADC module */
			STM32_ADC_CR2(id) |= (1 << 0);  /* ADON */

			/* Reset calibration */
			STM32_ADC_CR2(id) |= (1 << 3);  /* RSTCAL */
			while (STM32_ADC_CR2(id) & (1 << 3))
				;

			/* A/D Calibrate */
			STM32_ADC_CR2(id) |= (1 << 2);  /* CAL */
			while (STM32_ADC_CR2(id) & (1 << 2))
				;
		}

		/* Set right alignment */
		STM32_ADC_CR2(id) &= ~(1 << 11);

		/* Set sampling time to 28.5 cycles */
		STM32_ADC_SMPR2(id) = 0x3;

		/* Select AIN0 */
		STM32_ADC_SQR3(id) &= ~0x1f;

		/* Disable DMA */
		STM32_ADC_CR2(id) &= ~(1 << 8);

		/* Disable scan mode */
		STM32_ADC_CR1(id) &= ~(1 << 8);
	}
}

static void timers_init(void)
{
	STM32_TIM_CR1(3) = 0x0004; /* MSB */
	STM32_TIM_CR1(2) = 0x0004; /* LSB */

	STM32_TIM_CR2(3) = 0x0000;
	STM32_TIM_CR2(2) = 0x0020;

	STM32_TIM_SMCR(3) = 0x0007 | (1 << 4);
	STM32_TIM_SMCR(2) = 0x0000;

	STM32_TIM_ARR(3) = 0xffff;
	STM32_TIM_ARR(2) = 0xffff;

	STM32_TIM_PSC(3) = 0;
	STM32_TIM_PSC(2) = CPU_CLOCK / 1000000 - 1;

	STM32_TIM_EGR(3) = 0x0001;
	STM32_TIM_EGR(2) = 0x0001;

	STM32_TIM_DIER(3) = 0x0001;
	STM32_TIM_DIER(2) = 0x0000;

	STM32_TIM_CR1(3) |= 1;
	STM32_TIM_CR1(2) |= 1;

	STM32_TIM_CNT(3) = 0;
	STM32_TIM_CNT(2) = 0;

	task_enable_irq(STM32_IRQ_TIM3);
	task_enable_irq(STM32_IRQ_TIM2);
}

static void irq_init(void)
{
	/* clear all pending interrupts */
	CPU_NVIC_UNPEND(0) = 0xffffffff;
	/* enable global interrupts */
	asm("cpsie i");
}

void hardware_init(void)
{
	power_init();
	clock_init();
	pins_init();
	timers_init();
	adc_init();
	irq_init();
}
