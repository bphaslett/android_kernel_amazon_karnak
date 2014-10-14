/*
 *  Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data.com
 *	info@addi-data.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/sched.h>
#include <linux/interrupt.h>

struct addi_board {
	const char *name;
	int i_PCIEeprom;	/*  eeprom present or not */
	char *pc_EepromChip;	/*  type of chip */
	int i_NbrAiChannel;	/*  num of A/D chans */
	int i_NbrAiChannelDiff;	/*  num of A/D chans in diff mode */
	int i_AiChannelList;	/*  len of chanlist */
	int i_NbrAoChannel;	/*  num of D/A chans */
	int i_AiMaxdata;	/*  resolution of A/D */
	int i_AoMaxdata;	/*  resolution of D/A */
	const struct comedi_lrange *pr_AiRangelist;	/* rangelist for A/D */

	int i_NbrDiChannel;	/*  Number of DI channels */
	int i_NbrDoChannel;	/*  Number of DO channels */
	int i_DoMaxdata;	/*  data to set all channels high */

	int i_Timer;		/*    timer subdevice present or not */
	unsigned int ui_MinAcquisitiontimeNs;	/*  Minimum Acquisition in Nano secs */
	unsigned int ui_MinDelaytimeNs;	/*  Minimum Delay in Nano secs */
};

struct addi_private {
	int iobase;
	int i_IobaseAmcc;	/*  base+size for AMCC chip */
	int i_IobaseAddon;	/* addon base address */
	int i_IobaseReserved;
	unsigned int ui_AiActualScan;	/* how many scans we finished */
	unsigned int ui_AiNbrofChannels;	/*  how many channels is measured */
	unsigned int ui_AiChannelList[32];	/*  actual chanlist */
	unsigned int ui_AiReadData[32];
	unsigned short us_UseDma;	/*  To use Dma or not */
	unsigned char b_DmaDoubleBuffer;	/*  we can use double buffering */
	unsigned int ui_DmaActualBuffer;	/*  which buffer is used now */
	unsigned short *ul_DmaBufferVirtual[2];	/*  pointers to DMA buffer */
	dma_addr_t ul_DmaBufferHw[2];		/*  hw address of DMA buff */
	unsigned int ui_DmaBufferSize[2];	/*  size of dma buffer in bytes */
	unsigned int ui_DmaBufferUsesize[2];	/*  which size we may now used for transfer */
	unsigned char b_DigitalOutputRegister;	/*  Digital Output Register */
	unsigned char b_OutputMemoryStatus;
	unsigned char b_TimerSelectMode;	/*  Contain data written at iobase + 0C */
	unsigned char b_ModeSelectRegister;	/*  Contain data written at iobase + 0E */
	unsigned short us_OutputRegister;	/*  Contain data written at iobase + 0 */
	unsigned char b_Timer2Mode;	/*  Specify the timer 2 mode */
	unsigned char b_Timer2Interrupt;	/* Timer2  interrupt enable or disable */
	unsigned int ai_running:1;
	unsigned char b_InterruptMode;	/*  eoc eos or dma */
	unsigned char b_EocEosInterrupt;	/*  Enable disable eoc eos interrupt */
	unsigned int ui_EocEosConversionTime;
	unsigned char b_ExttrigEnable;	/* To enable or disable external trigger */

	/* Pointer to the current process */
	struct task_struct *tsk_Current;

	/* Parameters read from EEPROM overriding static board info */
	struct {
		int i_NbrAiChannel;	/*  num of A/D chans */
		int i_NbrAoChannel;	/*  num of D/A chans */
		int i_AiMaxdata;	/*  resolution of A/D */
		int i_AoMaxdata;	/*  resolution of D/A */
		int i_NbrDiChannel;	/*  Number of DI channels */
		int i_NbrDoChannel;	/*  Number of DO channels */
		int i_DoMaxdata;	/*  data to set all channels high */
		int i_Timer;		/*  timer subdevice present or not */
		unsigned int ui_MinAcquisitiontimeNs;
					/*  Minimum Acquisition in Nano secs */
		unsigned int ui_MinDelaytimeNs;
					/*  Minimum Delay in Nano secs */
	} s_EeParameters;
};
