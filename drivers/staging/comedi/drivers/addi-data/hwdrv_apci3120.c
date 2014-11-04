/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.

	ADDI-DATA GmbH
	Dieselstrasse 3
	D-77833 Ottersweier
	Tel: +19(0)7223/9493-0
	Fax: +49(0)7223/9493-92
	http://www.addi-data.com
	info@addi-data.com

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

@endverbatim
*/
/*
  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstrasse 3      D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-----------------------------------------------------------------------+
  | Project     : APCI-3120       | Compiler   : GCC                      |
  | Module name : hwdrv_apci3120.c| Version    : 2.96                     |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date       :  02/12/2002              |
  +-----------------------------------------------------------------------+
  | Description :APCI3120 Module.  Hardware abstraction Layer for APCI3120|
  +-----------------------------------------------------------------------+
  |                             UPDATE'S                                  |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  |          | 		 | 						  |
  |          |           |						  |
  +----------+-----------+------------------------------------------------+
*/

/*
 * ADDON RELATED ADDITIONS
 */
/* Constant */
#define APCI3120_ENABLE_TRANSFER_ADD_ON_LOW		0x00
#define APCI3120_ENABLE_TRANSFER_ADD_ON_HIGH		0x1200
#define APCI3120_A2P_FIFO_MANAGEMENT			0x04000400L
#define APCI3120_AMWEN_ENABLE				0x02
#define APCI3120_A2P_FIFO_WRITE_ENABLE			0x01
#define APCI3120_FIFO_ADVANCE_ON_BYTE_2			0x20000000L
#define APCI3120_ENABLE_WRITE_TC_INT			0x00004000L
#define APCI3120_CLEAR_WRITE_TC_INT			0x00040000L
#define APCI3120_DISABLE_AMWEN_AND_A2P_FIFO_WRITE	0x0
#define APCI3120_DISABLE_BUS_MASTER_ADD_ON		0x0
#define APCI3120_DISABLE_BUS_MASTER_PCI			0x0

/* ADD_ON ::: this needed since apci supports 16 bit interface to add on */
#define APCI3120_ADD_ON_AGCSTS_LOW	0x3C
#define APCI3120_ADD_ON_AGCSTS_HIGH	(APCI3120_ADD_ON_AGCSTS_LOW + 2)
#define APCI3120_ADD_ON_MWAR_LOW	0x24
#define APCI3120_ADD_ON_MWAR_HIGH	(APCI3120_ADD_ON_MWAR_LOW + 2)
#define APCI3120_ADD_ON_MWTC_LOW	0x058
#define APCI3120_ADD_ON_MWTC_HIGH	(APCI3120_ADD_ON_MWTC_LOW + 2)

/* AMCC */
#define APCI3120_AMCC_OP_MCSR		0x3C
#define APCI3120_AMCC_OP_REG_INTCSR	0x38

/* for transfer count enable bit */
#define AGCSTS_TC_ENABLE	0x10000000

#define APCI3120_DISABLE		0
#define APCI3120_ENABLE			1

#define APCI3120_START			1
#define APCI3120_STOP			0

#define APCI3120_EOC_MODE		1
#define APCI3120_EOS_MODE		2
#define APCI3120_DMA_MODE		3

#define APCI3120_RD_STATUS		0x02
#define APCI3120_RD_FIFO		0x00

/* nWrMode_Select */
#define APCI3120_ENABLE_SCAN		0x8
#define APCI3120_DISABLE_SCAN		(~APCI3120_ENABLE_SCAN)
#define APCI3120_ENABLE_EOS_INT		0x2

#define APCI3120_DISABLE_EOS_INT	(~APCI3120_ENABLE_EOS_INT)
#define APCI3120_ENABLE_EOC_INT		0x1
#define APCI3120_DISABLE_EOC_INT	(~APCI3120_ENABLE_EOC_INT)
#define APCI3120_DISABLE_ALL_INTERRUPT_WITHOUT_TIMER	\
	(APCI3120_DISABLE_EOS_INT & APCI3120_DISABLE_EOC_INT)
#define APCI3120_DISABLE_ALL_INTERRUPT			\
	(APCI3120_DISABLE_TIMER_INT & APCI3120_DISABLE_EOS_INT & APCI3120_DISABLE_EOC_INT)

/* status register bits */
#define APCI3120_EOC			0x8000
#define APCI3120_EOS			0x2000

/* software trigger dummy register */
#define APCI3120_START_CONVERSION	0x02

/* TIMER DEFINE */
#define APCI3120_QUARTZ_A		70
#define APCI3120_QUARTZ_B		50
#define APCI3120_TIMER			1
#define APCI3120_WATCHDOG		2
#define APCI3120_TIMER_DISABLE		0
#define APCI3120_TIMER_ENABLE		1
#define APCI3120_ENABLE_TIMER_INT	0x04
#define APCI3120_DISABLE_TIMER_INT	(~APCI3120_ENABLE_TIMER_INT)
#define APCI3120_WRITE_MODE_SELECT	0x0e

#define APCI3120_RD_STATUS		0x02
#define APCI3120_ENABLE_WATCHDOG	0x20
#define APCI3120_DISABLE_WATCHDOG	(~APCI3120_ENABLE_WATCHDOG)
#define APCI3120_ENABLE_TIMER_COUNTER	0x10
#define APCI3120_DISABLE_TIMER_COUNTER	(~APCI3120_ENABLE_TIMER_COUNTER)
#define APCI3120_FC_TIMER		0x1000

#define APCI3120_TIMER2_SELECT_EOS	0xc0
#define APCI3120_COUNTER		3

static int apci3120_ai_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned int i;

	if ((data[0] != APCI3120_EOC_MODE) && (data[0] != APCI3120_EOS_MODE))
		return -1;

	/*  Check for Conversion time to be added */
	devpriv->ui_EocEosConversionTime = data[2];

	if (data[0] == APCI3120_EOS_MODE) {

		/* Test the number of the channel */
		for (i = 0; i < data[3]; i++) {

			if (CR_CHAN(data[4 + i]) >= s->n_chan) {
				dev_err(dev->class_dev, "bad channel list\n");
				return -2;
			}
		}

		devpriv->b_InterruptMode = APCI3120_EOS_MODE;

		if (data[1])
			devpriv->b_EocEosInterrupt = APCI3120_ENABLE;
		else
			devpriv->b_EocEosInterrupt = APCI3120_DISABLE;
		/*  Copy channel list and Range List to devpriv */
		devpriv->ui_AiNbrofChannels = data[3];
		for (i = 0; i < devpriv->ui_AiNbrofChannels; i++)
			devpriv->ui_AiChannelList[i] = data[4 + i];

	} else {			/*  EOC */
		devpriv->b_InterruptMode = APCI3120_EOC_MODE;
		if (data[1])
			devpriv->b_EocEosInterrupt = APCI3120_ENABLE;
		else
			devpriv->b_EocEosInterrupt = APCI3120_DISABLE;
	}

	return insn->n;
}

static int apci3120_setup_chan_list(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    int n_chan, unsigned int *chanlist)
{
	struct apci3120_private *devpriv = dev->private;
	int i;

	/* correct channel and range number check itself comedi/range.c */
	if (n_chan < 1) {
		dev_err(dev->class_dev, "range/channel list is empty!\n");
		return 0;
	}

	/* set scan length (PR) and scan start (PA) */
	devpriv->ctrl = APCI3120_CTRL_PR(n_chan - 1) | APCI3120_CTRL_PA(0);
	outw(devpriv->ctrl, dev->iobase + APCI3120_CTRL_REG);

	/* set chanlist for scan */
	for (i = 0; i < n_chan; i++) {
		unsigned int chan = CR_CHAN(chanlist[i]);
		unsigned int range = CR_RANGE(chanlist[i]);
		unsigned int val;

		val = APCI3120_CHANLIST_MUX(chan) |
		      APCI3120_CHANLIST_GAIN(range) |
		      APCI3120_CHANLIST_INDEX(i);

		if (comedi_range_is_unipolar(s, range))
			val |= APCI3120_CHANLIST_UNIPOLAR;

		outw(val, dev->iobase + APCI3120_CHANLIST_REG);
	}
	return 1;		/*  we can serve this with scan logic */
}

/*
 * Reads analog input in synchronous mode EOC and EOS is selected
 * as per configured if no conversion time is set uses default
 * conversion time 10 microsec.
 */
static int apci3120_ai_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned int divisor;
	unsigned int ns;
	unsigned short us_TmpValue, i;

	/*  fix conversion time to 10 us */
	if (!devpriv->ui_EocEosConversionTime)
		ns = 10000;
	else
		ns = devpriv->ui_EocEosConversionTime;

	/*  Clear software registers */
	devpriv->timer_mode = 0;
	devpriv->b_ModeSelectRegister = 0;

	if (insn->unused[0] == 222) {	/*  second insn read */
		for (i = 0; i < insn->n; i++)
			data[i] = devpriv->ui_AiReadData[i];
	} else {
		devpriv->tsk_Current = current;	/*  Save the current process task structure */

		divisor = apci3120_ns_to_timer(dev, 0, ns, CMDF_ROUND_NEAREST);

		us_TmpValue = (unsigned short) devpriv->b_InterruptMode;

		switch (us_TmpValue) {

		case APCI3120_EOC_MODE:
			apci3120_ai_reset_fifo(dev);

			/*  Initialize the sequence array */
			if (!apci3120_setup_chan_list(dev, s, 1,
					&insn->chanspec))
				return -EINVAL;

			/* Initialize Timer 0 mode 4 */
			apci3120_timer_set_mode(dev, 0, APCI3120_TIMER_MODE4);

			/*  Reset the scan bit and Disables the  EOS, DMA, EOC interrupt */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister & APCI3120_DISABLE_SCAN;

			if (devpriv->b_EocEosInterrupt == APCI3120_ENABLE) {

				/* Disables the EOS,DMA and enables the EOC interrupt */
				devpriv->b_ModeSelectRegister =
					(devpriv->
					b_ModeSelectRegister &
					APCI3120_DISABLE_EOS_INT) |
					APCI3120_ENABLE_EOC_INT;
				inw(dev->iobase + 0);

			} else {
				devpriv->b_ModeSelectRegister =
					devpriv->
					b_ModeSelectRegister &
					APCI3120_DISABLE_ALL_INTERRUPT_WITHOUT_TIMER;
			}

			outb(devpriv->b_ModeSelectRegister,
			     dev->iobase + APCI3120_WRITE_MODE_SELECT);

			apci3120_timer_enable(dev, 0, true);

			/* Set the conversion time */
			apci3120_timer_write(dev, 0, divisor);

			us_TmpValue =
				(unsigned short) inw(dev->iobase + APCI3120_RD_STATUS);

			if (devpriv->b_EocEosInterrupt == APCI3120_DISABLE) {

				do {
					/*  Waiting for the end of conversion */
					us_TmpValue = inw(dev->iobase +
							  APCI3120_RD_STATUS);
				} while ((us_TmpValue & APCI3120_EOC) ==
					APCI3120_EOC);

				/* Read the result in FIFO  and put it in insn data pointer */
				us_TmpValue = inw(dev->iobase + 0);
				*data = us_TmpValue;

				apci3120_ai_reset_fifo(dev);
			}

			break;

		case APCI3120_EOS_MODE:
			apci3120_ai_reset_fifo(dev);

			if (!apci3120_setup_chan_list(dev, s,
					devpriv->ui_AiNbrofChannels,
					devpriv->ui_AiChannelList))
				return -EINVAL;

			/* Initialize Timer 0 mode 2 */
			apci3120_timer_set_mode(dev, 0, APCI3120_TIMER_MODE2);

			/* Set the conversion time */
			apci3120_timer_write(dev, 0, divisor);

			/* Set the scan bit */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister | APCI3120_ENABLE_SCAN;
			outb(devpriv->b_ModeSelectRegister,
			     dev->iobase + APCI3120_WRITE_MODE_SELECT);

			/* If Interrupt function is loaded */
			if (devpriv->b_EocEosInterrupt == APCI3120_ENABLE) {
				/* Disables the EOC,DMA and enables the EOS interrupt */
				devpriv->b_ModeSelectRegister =
					(devpriv->
					b_ModeSelectRegister &
					APCI3120_DISABLE_EOC_INT) |
					APCI3120_ENABLE_EOS_INT;
				inw(dev->iobase + 0);

			} else
				devpriv->b_ModeSelectRegister =
					devpriv->
					b_ModeSelectRegister &
					APCI3120_DISABLE_ALL_INTERRUPT_WITHOUT_TIMER;

			outb(devpriv->b_ModeSelectRegister,
			     dev->iobase + APCI3120_WRITE_MODE_SELECT);

			inw(dev->iobase + APCI3120_RD_STATUS);

			apci3120_timer_enable(dev, 0, true);

			/* Start conversion */
			outw(0, dev->iobase + APCI3120_START_CONVERSION);

			/* Waiting of end of conversion if interrupt is not installed */
			if (devpriv->b_EocEosInterrupt == APCI3120_DISABLE) {
				/* Waiting the end of conversion */
				do {
					us_TmpValue = inw(dev->iobase +
							  APCI3120_RD_STATUS);
				} while ((us_TmpValue & APCI3120_EOS) !=
					 APCI3120_EOS);

				for (i = 0; i < devpriv->ui_AiNbrofChannels;
					i++) {
					/* Read the result in FIFO and write them in shared memory */
					us_TmpValue = inw(dev->iobase);
					data[i] = (unsigned int) us_TmpValue;
				}

				devpriv->b_InterruptMode = APCI3120_EOC_MODE;	/*  Restore defaults */
			}
			break;

		default:
			dev_err(dev->class_dev, "inputs wrong\n");

		}
		devpriv->ui_EocEosConversionTime = 0;	/*  re initializing the variable */
	}

	return insn->n;

}

static int apci3120_reset(struct comedi_device *dev)
{
	struct apci3120_private *devpriv = dev->private;

	devpriv->ai_running = 0;
	devpriv->b_EocEosInterrupt = APCI3120_DISABLE;
	devpriv->b_InterruptMode = APCI3120_EOC_MODE;
	devpriv->ui_EocEosConversionTime = 0;	/*  set eoc eos conv time to 0 */

	/*  variables used in timer subdevice */
	devpriv->b_Timer2Mode = 0;
	devpriv->b_Timer2Interrupt = 0;
	devpriv->b_ExttrigEnable = 0;	/*  Disable ext trigger */

	/* Disable all interrupts, watchdog for the anolog output */
	devpriv->b_ModeSelectRegister = 0;
	outb(devpriv->b_ModeSelectRegister,
		dev->iobase + APCI3120_WRITE_MODE_SELECT);

	/* disable all counters, ext trigger, and reset scan */
	devpriv->ctrl = 0;
	outw(devpriv->ctrl, dev->iobase + APCI3120_CTRL_REG);

	apci3120_ai_reset_fifo(dev);
	inw(dev->iobase + APCI3120_RD_STATUS);	/*  flush A/D status register */

	return 0;
}

static int apci3120_cancel(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct apci3120_private *devpriv = dev->private;

	/*  Disable A2P Fifo write and AMWEN signal */
	outw(0, devpriv->addon + 4);

	/* Disable Bus Master ADD ON */
	outw(APCI3120_ADD_ON_AGCSTS_LOW, devpriv->addon + 0);
	outw(0, devpriv->addon + 2);
	outw(APCI3120_ADD_ON_AGCSTS_HIGH, devpriv->addon + 0);
	outw(0, devpriv->addon + 2);

	/* Disable BUS Master PCI */
	outl(0, devpriv->amcc + AMCC_OP_REG_MCSR);

	/* disable all counters, ext trigger, and reset scan */
	devpriv->ctrl = 0;
	outw(devpriv->ctrl, dev->iobase + APCI3120_CTRL_REG);

	/* DISABLE_ALL_INTERRUPT */
	outb(APCI3120_DISABLE_ALL_INTERRUPT,
		dev->iobase + APCI3120_WRITE_MODE_SELECT);

	apci3120_ai_reset_fifo(dev);
	inw(dev->iobase + APCI3120_RD_STATUS);
	devpriv->ui_DmaActualBuffer = 0;

	devpriv->ai_running = 0;
	devpriv->b_InterruptMode = APCI3120_EOC_MODE;
	devpriv->b_EocEosInterrupt = APCI3120_DISABLE;

	return 0;
}

static int apci3120_ai_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_FOLLOW);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_TIMER);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_TIMER)	/* Test Delay timing */
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg, 100000);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		if (cmd->convert_arg)
			err |= cfc_check_trigger_arg_min(&cmd->convert_arg,
							 10000);
	} else {
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg, 10000);
	}

	err |= cfc_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/*  TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/*  step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER &&
	    cmd->scan_begin_arg < cmd->convert_arg * cmd->scan_end_arg) {
		cmd->scan_begin_arg = cmd->convert_arg * cmd->scan_end_arg;
		err |= -EINVAL;
	}

	if (err)
		return 4;

	return 0;
}

/*
 * This is used for analog input cyclic acquisition.
 * Performs the command operations.
 * If DMA is configured does DMA initialization otherwise does the
 * acquisition with EOS interrupt.
 */
static int apci3120_cyclic_ai(int mode,
			      struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct apci3120_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int divisor1 = 0;
	unsigned int dmalen0 = 0;
	unsigned int dmalen1 = 0;
	unsigned int divisor0;

	devpriv->ai_running = 1;

	/*  clear software  registers */
	devpriv->timer_mode = 0;
	devpriv->b_ModeSelectRegister = 0;

	/* Clear Timer Write TC int */
	outl(APCI3120_CLEAR_WRITE_TC_INT,
	     devpriv->amcc + APCI3120_AMCC_OP_REG_INTCSR);

	apci3120_ai_reset_fifo(dev);

	devpriv->ui_DmaActualBuffer = 0;

	/* Initializes the sequence array */
	if (!apci3120_setup_chan_list(dev, s, devpriv->ui_AiNbrofChannels,
			cmd->chanlist))
		return -EINVAL;

	divisor0 = apci3120_ns_to_timer(dev, 0, cmd->convert_arg, cmd->flags);
	if (mode == 2) {
		divisor1 = apci3120_ns_to_timer(dev, 1, cmd->scan_begin_arg,
						cmd->flags);
	}

	if (devpriv->b_ExttrigEnable == APCI3120_ENABLE)
		apci3120_exttrig_enable(dev, true);

	switch (mode) {
	case 1:
		/*  init timer0 in mode 2 */
		apci3120_timer_set_mode(dev, 0, APCI3120_TIMER_MODE2);

		/* Set the conversion time */
		apci3120_timer_write(dev, 0, divisor0);
		break;

	case 2:
		/*  init timer1 in mode 2 */
		apci3120_timer_set_mode(dev, 1, APCI3120_TIMER_MODE2);

		/* Set the scan begin time */
		apci3120_timer_write(dev, 1, divisor1);

		/*  init timer0 in mode 2 */
		apci3120_timer_set_mode(dev, 0, APCI3120_TIMER_MODE2);

		/* Set the conversion time */
		apci3120_timer_write(dev, 0, divisor0);
		break;

	}
	/* common for all modes */
	/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
	devpriv->b_ModeSelectRegister = devpriv->b_ModeSelectRegister &
		APCI3120_DISABLE_SCAN;
	/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

	outb(devpriv->b_ModeSelectRegister,
		dev->iobase + APCI3120_WRITE_MODE_SELECT);

	/*  If DMA is disabled */
	if (devpriv->us_UseDma == APCI3120_DISABLE) {
		/*  disable EOC and enable EOS */
		devpriv->b_InterruptMode = APCI3120_EOS_MODE;
		devpriv->b_EocEosInterrupt = APCI3120_ENABLE;

		devpriv->b_ModeSelectRegister =
			(devpriv->
			b_ModeSelectRegister & APCI3120_DISABLE_EOC_INT) |
			APCI3120_ENABLE_EOS_INT;
		outb(devpriv->b_ModeSelectRegister,
			dev->iobase + APCI3120_WRITE_MODE_SELECT);

		if (cmd->stop_src == TRIG_COUNT) {
			/* configure Timer2 For counting EOS */

			/*  DISABLE TIMER intERRUPT */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister &
				APCI3120_DISABLE_TIMER_INT & 0xEF;
			outb(devpriv->b_ModeSelectRegister,
				dev->iobase + APCI3120_WRITE_MODE_SELECT);

			/* (1) Init timer 2 in mode 0 and write timer value */
			apci3120_timer_set_mode(dev, 2, APCI3120_TIMER_MODE0);

			/* Set the scan stop count (not sure about the -2) */
			apci3120_timer_write(dev, 2, cmd->stop_arg - 2);

			apci3120_clr_timer2_interrupt(dev);

			/*  enable timer counter and disable watch dog */
			devpriv->b_ModeSelectRegister =
				(devpriv->
				b_ModeSelectRegister |
				APCI3120_ENABLE_TIMER_COUNTER) &
				APCI3120_DISABLE_WATCHDOG;
			/*  select EOS clock input for timer 2 */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister |
				APCI3120_TIMER2_SELECT_EOS;
			/*  Enable timer2  interrupt */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister |
				APCI3120_ENABLE_TIMER_INT;
			outb(devpriv->b_ModeSelectRegister,
				dev->iobase + APCI3120_WRITE_MODE_SELECT);
			devpriv->b_Timer2Mode = APCI3120_COUNTER;
			devpriv->b_Timer2Interrupt = APCI3120_ENABLE;
		}
	} else {
		/* If DMA Enabled */
		struct apci3120_dmabuf *dmabuf0 = &devpriv->dmabuf[0];
		struct apci3120_dmabuf *dmabuf1 = &devpriv->dmabuf[1];
		unsigned int scan_bytes;

		scan_bytes = comedi_samples_to_bytes(s, cmd->scan_end_arg);

		devpriv->b_InterruptMode = APCI3120_DMA_MODE;

		/* Disables the EOC, EOS interrupt  */
		devpriv->b_ModeSelectRegister = devpriv->b_ModeSelectRegister &
			APCI3120_DISABLE_EOC_INT & APCI3120_DISABLE_EOS_INT;

		outb(devpriv->b_ModeSelectRegister,
			dev->iobase + APCI3120_WRITE_MODE_SELECT);

		dmalen0 = dmabuf0->size;
		dmalen1 = dmabuf1->size;

		if (cmd->stop_src == TRIG_COUNT) {
			/*
			 * Must we fill full first buffer? And must we fill
			 * full second buffer when first is once filled?
			 */
			if (dmalen0 > (cmd->stop_arg * scan_bytes)) {
				dmalen0 = cmd->stop_arg * scan_bytes;
			} else if (dmalen1 > (cmd->stop_arg * scan_bytes -
					      dmalen0))
				dmalen1 = cmd->stop_arg * scan_bytes -
					  dmalen0;
		}

		if (cmd->flags & CMDF_WAKE_EOS) {
			/*  don't we want wake up every scan? */
			if (dmalen0 > scan_bytes) {
				dmalen0 = scan_bytes;
				if (cmd->scan_end_arg & 1)
					dmalen0 += 2;
			}
			if (dmalen1 > scan_bytes) {
				dmalen1 = scan_bytes;
				if (cmd->scan_end_arg & 1)
					dmalen1 -= 2;
				if (dmalen1 < 4)
					dmalen1 = 4;
			}
		} else {	/*  isn't output buff smaller that our DMA buff? */
			if (dmalen0 > s->async->prealloc_bufsz)
				dmalen0 = s->async->prealloc_bufsz;
			if (dmalen1 > s->async->prealloc_bufsz)
				dmalen1 = s->async->prealloc_bufsz;
		}
		dmabuf0->use_size = dmalen0;
		dmabuf1->use_size = dmalen1;

		/* Initialize DMA */

		/*
		 * Set Transfer count enable bit and A2P_fifo reset bit in AGCSTS
		 * register 1
		 */
		outl(AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
		     devpriv->amcc + AMCC_OP_REG_AGCSTS);

		/*  changed  since 16 bit interface for add on */
		/* ENABLE BUS MASTER */
		outw(APCI3120_ADD_ON_AGCSTS_LOW, devpriv->addon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_LOW, devpriv->addon + 2);

		outw(APCI3120_ADD_ON_AGCSTS_HIGH, devpriv->addon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_HIGH, devpriv->addon + 2);

		/*
		 * TO VERIFIED BEGIN JK 07.05.04: Comparison between WIN32 and Linux
		 * driver
		 */
		outw(0x1000, devpriv->addon + 2);
		/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

		/* 2 No change */
		/* A2P FIFO MANAGEMENT */
		/* A2P fifo reset & transfer control enable */
		outl(APCI3120_A2P_FIFO_MANAGEMENT,
		     devpriv->amcc + APCI3120_AMCC_OP_MCSR);

		/*
		 * 3
		 * beginning address of dma buf The 32 bit address of dma buffer
		 * is converted into two 16 bit addresses Can done by using _attach
		 * and put into into an array array used may be for differnet pages
		 */

		/*  DMA Start Address Low */
		outw(APCI3120_ADD_ON_MWAR_LOW, devpriv->addon + 0);
		outw(dmabuf0->hw & 0xffff, devpriv->addon + 2);

		/* DMA Start Address High */
		outw(APCI3120_ADD_ON_MWAR_HIGH, devpriv->addon + 0);
		outw((dmabuf0->hw >> 16) & 0xffff, devpriv->addon + 2);

		/*
		 * 4
		 * amount of bytes to be transferred set transfer count used ADDON
		 * MWTC register commented testing
		 */

		/* Nbr of acquisition LOW */
		outw(APCI3120_ADD_ON_MWTC_LOW, devpriv->addon + 0);
		outw(dmabuf0->use_size & 0xffff, devpriv->addon + 2);

		/* Nbr of acquisition HIGH */
		outw(APCI3120_ADD_ON_MWTC_HIGH, devpriv->addon + 0);
		outw((dmabuf0->use_size >> 16) & 0xffff, devpriv->addon + 2);

		/*
		 * 5
		 * To configure A2P FIFO testing outl(
		 * FIFO_ADVANCE_ON_BYTE_2, devpriv->amcc + AMCC_OP_REG_INTCSR);
		 */

		/* A2P FIFO RESET */
		/*
		 * TO VERIFY BEGIN JK 07.05.04: Comparison between WIN32 and Linux
		 * driver
		 */
		outl(0x04000000UL, devpriv->amcc + AMCC_OP_REG_MCSR);
		/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

		/*
		 * 6
		 * ENABLE A2P FIFO WRITE AND ENABLE AMWEN AMWEN_ENABLE |
		 * A2P_FIFO_WRITE_ENABLE (0x01|0x02)=0x03
		 */

		/*
		 * 7
		 * initialise end of dma interrupt AINT_WRITE_COMPL =
		 * ENABLE_WRITE_TC_INT(ADDI)
		 */
		/* A2P FIFO CONFIGURATE, END OF DMA intERRUPT INIT */
		outl((APCI3120_FIFO_ADVANCE_ON_BYTE_2 |
				APCI3120_ENABLE_WRITE_TC_INT),
			devpriv->amcc + AMCC_OP_REG_INTCSR);

		/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
		/* ENABLE A2P FIFO WRITE AND ENABLE AMWEN */
		outw(3, devpriv->addon + 4);
		/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

		/* A2P FIFO RESET */
		/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
		outl(0x04000000UL, devpriv->amcc + APCI3120_AMCC_OP_MCSR);
		/* END JK 07.05.04: Comparison between WIN32 and Linux driver */
	}

	if (devpriv->us_UseDma == APCI3120_DISABLE &&
	    cmd->stop_src == TRIG_COUNT)
		apci3120_timer_enable(dev, 2, true);

	switch (mode) {
	case 1:
		apci3120_timer_enable(dev, 0, true);
		break;
	case 2:
		apci3120_timer_enable(dev, 1, true);
		apci3120_timer_enable(dev, 0, true);
		break;
	}

	return 0;

}

/*
 * Does asynchronous acquisition.
 * Determines the mode 1 or 2.
 */
static int apci3120_ai_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct apci3120_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;

	/* loading private structure with cmd structure inputs */
	devpriv->ui_AiNbrofChannels = cmd->chanlist_len;

	if (cmd->start_src == TRIG_EXT)
		devpriv->b_ExttrigEnable = APCI3120_ENABLE;
	else
		devpriv->b_ExttrigEnable = APCI3120_DISABLE;

	if (cmd->scan_begin_src == TRIG_FOLLOW)
		return apci3120_cyclic_ai(1, dev, s);
	/* TRIG_TIMER */
	return apci3120_cyclic_ai(2, dev, s);
}

/*
 * This is a handler for the DMA interrupt.
 * This function copies the data to Comedi Buffer.
 * For continuous DMA it reinitializes the DMA operation.
 * For single mode DMA it stop the acquisition.
 */
static void apci3120_interrupt_dma(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct apci3120_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_cmd *cmd = &s->async->cmd;
	struct apci3120_dmabuf *dmabuf;
	unsigned int samplesinbuf;
	unsigned int ui_Tmp;

	dmabuf = &devpriv->dmabuf[devpriv->ui_DmaActualBuffer];

	samplesinbuf = dmabuf->use_size - inl(devpriv->amcc + AMCC_OP_REG_MWTC);

	if (samplesinbuf < dmabuf->use_size)
		dev_err(dev->class_dev, "Interrupted DMA transfer!\n");
	if (samplesinbuf & 1) {
		dev_err(dev->class_dev, "Odd count of bytes in DMA ring!\n");
		apci3120_cancel(dev, s);
		return;
	}
	samplesinbuf = samplesinbuf >> 1;	/*  number of received samples */
	if (devpriv->b_DmaDoubleBuffer) {
		/*  switch DMA buffers if is used double buffering */
		struct apci3120_dmabuf *next_dmabuf;

		next_dmabuf = &devpriv->dmabuf[1 - devpriv->ui_DmaActualBuffer];

		ui_Tmp = AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO;
		outl(ui_Tmp, devpriv->amcc + AMCC_OP_REG_AGCSTS);

		/*  changed  since 16 bit interface for add on */
		outw(APCI3120_ADD_ON_AGCSTS_LOW, devpriv->addon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_LOW, devpriv->addon + 2);
		outw(APCI3120_ADD_ON_AGCSTS_HIGH, devpriv->addon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_HIGH, devpriv->addon + 2);	/*  0x1000 is out putted in windows driver */

		/* DMA Start Address Low */
		outw(APCI3120_ADD_ON_MWAR_LOW, devpriv->addon + 0);
		outw(next_dmabuf->hw & 0xffff, devpriv->addon + 2);

		/* DMA Start Address High */
		outw(APCI3120_ADD_ON_MWAR_HIGH, devpriv->addon + 0);
		outw((next_dmabuf->hw >> 16) & 0xffff, devpriv->addon + 2);

		/* Nbr of acquisition LOW */
		outw(APCI3120_ADD_ON_MWTC_LOW, devpriv->addon + 0);
		outw(next_dmabuf->use_size & 0xffff, devpriv->addon + 2);

		/* Nbr of acquisition HIGH */
		outw(APCI3120_ADD_ON_MWTC_HIGH, devpriv->addon + 0);
		outw((next_dmabuf->use_size > 16) & 0xffff, devpriv->addon + 2);

		/*
		 * To configure A2P FIFO
		 * ENABLE A2P FIFO WRITE AND ENABLE AMWEN
		 * AMWEN_ENABLE | A2P_FIFO_WRITE_ENABLE (0x01|0x02)=0x03
		 */
		outw(3, devpriv->addon + 4);
		/* initialise end of dma interrupt  AINT_WRITE_COMPL = ENABLE_WRITE_TC_INT(ADDI) */
		outl(APCI3120_FIFO_ADVANCE_ON_BYTE_2 |
		     APCI3120_ENABLE_WRITE_TC_INT,
		     devpriv->amcc + AMCC_OP_REG_INTCSR);

	}
	if (samplesinbuf) {
		comedi_buf_write_samples(s, dmabuf->virt, samplesinbuf);

		if (!(cmd->flags & CMDF_WAKE_EOS))
			s->async->events |= COMEDI_CB_EOS;
	}
	if (cmd->stop_src == TRIG_COUNT &&
	    s->async->scans_done >= cmd->stop_arg) {
		s->async->events |= COMEDI_CB_EOA;
		return;
	}

	if (devpriv->b_DmaDoubleBuffer) {	/*  switch dma buffers */
		devpriv->ui_DmaActualBuffer = 1 - devpriv->ui_DmaActualBuffer;
	} else {
		/*
		 * restart DMA if is not used double buffering
		 * ADDED REINITIALISE THE DMA
		 */
		outl(AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
		     devpriv->amcc + AMCC_OP_REG_AGCSTS);

		/*  changed  since 16 bit interface for add on */
		outw(APCI3120_ADD_ON_AGCSTS_LOW, devpriv->addon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_LOW, devpriv->addon + 2);
		outw(APCI3120_ADD_ON_AGCSTS_HIGH, devpriv->addon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_HIGH, devpriv->addon + 2);
		/*
		 * A2P FIFO MANAGEMENT
		 * A2P fifo reset & transfer control enable
		 */
		outl(APCI3120_A2P_FIFO_MANAGEMENT,
		     devpriv->amcc + AMCC_OP_REG_MCSR);

		outw(APCI3120_ADD_ON_MWAR_LOW, devpriv->addon + 0);
		outw(dmabuf->hw & 0xffff, devpriv->addon + 2);
		outw(APCI3120_ADD_ON_MWAR_HIGH, devpriv->addon + 0);
		outw((dmabuf->hw >> 16) & 0xffff, devpriv->addon + 2);

		outw(APCI3120_ADD_ON_MWTC_LOW, devpriv->addon + 0);
		outw(dmabuf->use_size & 0xffff, devpriv->addon + 2);
		outw(APCI3120_ADD_ON_MWTC_HIGH, devpriv->addon + 0);
		outw((dmabuf->use_size >> 16) & 0xffff, devpriv->addon + 2);

		/*
		 * To configure A2P FIFO
		 * ENABLE A2P FIFO WRITE AND ENABLE AMWEN
		 * AMWEN_ENABLE | A2P_FIFO_WRITE_ENABLE (0x01|0x02)=0x03
		 */
		outw(3, devpriv->addon + 4);
		/* initialise end of dma interrupt  AINT_WRITE_COMPL = ENABLE_WRITE_TC_INT(ADDI) */
		outl(APCI3120_FIFO_ADVANCE_ON_BYTE_2 |
		     APCI3120_ENABLE_WRITE_TC_INT,
		     devpriv->amcc + AMCC_OP_REG_INTCSR);
	}
}

/*
 * This function handles EOS interrupt.
 * This function copies the acquired data(from FIFO) to Comedi buffer.
 */
static int apci3120_interrupt_handle_eos(struct comedi_device *dev)
{
	struct apci3120_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned short val;
	int i;

	for (i = 0; i < devpriv->ui_AiNbrofChannels; i++) {
		val = inw(dev->iobase + 0);
		comedi_buf_write_samples(s, &val, 1);
	}

	return 0;
}

static irqreturn_t apci3120_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct apci3120_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned short int_daq;
	unsigned int int_amcc, ui_Check, i;
	unsigned short us_TmpValue;

	ui_Check = 1;

	int_daq = inw(dev->iobase + APCI3120_RD_STATUS) & 0xf000;	/*  get IRQ reasons */
	int_amcc = inl(devpriv->amcc + AMCC_OP_REG_INTCSR);

	if ((!int_daq) && (!(int_amcc & ANY_S593X_INT))) {
		dev_err(dev->class_dev, "IRQ from unknown source\n");
		return IRQ_NONE;
	}

	outl(int_amcc | 0x00ff0000, devpriv->amcc + AMCC_OP_REG_INTCSR);

	int_daq = (int_daq >> 12) & 0xF;

	if (devpriv->b_ExttrigEnable == APCI3120_ENABLE) {
		apci3120_exttrig_enable(dev, false);
		devpriv->b_ExttrigEnable = APCI3120_DISABLE;
	}

	apci3120_clr_timer2_interrupt(dev);

	if (int_amcc & MASTER_ABORT_INT)
		dev_err(dev->class_dev, "AMCC IRQ - MASTER DMA ABORT!\n");
	if (int_amcc & TARGET_ABORT_INT)
		dev_err(dev->class_dev, "AMCC IRQ - TARGET DMA ABORT!\n");

	/*  Ckeck if EOC interrupt */
	if (((int_daq & 0x8) == 0)
		&& (devpriv->b_InterruptMode == APCI3120_EOC_MODE)) {
		if (devpriv->b_EocEosInterrupt == APCI3120_ENABLE) {

			/*  Read the AI Value */
			devpriv->ui_AiReadData[0] = inw(dev->iobase + 0);
			devpriv->b_EocEosInterrupt = APCI3120_DISABLE;
			send_sig(SIGIO, devpriv->tsk_Current, 0);	/*  send signal to the sample */
		} else {
			/* Disable EOC Interrupt */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister & APCI3120_DISABLE_EOC_INT;
			outb(devpriv->b_ModeSelectRegister,
			     dev->iobase + APCI3120_WRITE_MODE_SELECT);
		}
	}

	/*  Check If EOS interrupt */
	if ((int_daq & 0x2) && (devpriv->b_InterruptMode == APCI3120_EOS_MODE)) {

		if (devpriv->b_EocEosInterrupt == APCI3120_ENABLE) {	/*  enable this in without DMA ??? */

			if (devpriv->ai_running) {
				ui_Check = 0;
				apci3120_interrupt_handle_eos(dev);
				devpriv->b_ModeSelectRegister =
					devpriv->
					b_ModeSelectRegister |
					APCI3120_ENABLE_EOS_INT;
				outb(devpriv->b_ModeSelectRegister,
					dev->iobase +
					APCI3120_WRITE_MODE_SELECT);
			} else {
				ui_Check = 0;
				for (i = 0; i < devpriv->ui_AiNbrofChannels;
					i++) {
					us_TmpValue = inw(dev->iobase + 0);
					devpriv->ui_AiReadData[i] =
						(unsigned int) us_TmpValue;
				}
				devpriv->b_EocEosInterrupt = APCI3120_DISABLE;
				devpriv->b_InterruptMode = APCI3120_EOC_MODE;

				send_sig(SIGIO, devpriv->tsk_Current, 0);	/*  send signal to the sample */

			}

		} else {
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister & APCI3120_DISABLE_EOS_INT;
			outb(devpriv->b_ModeSelectRegister,
				dev->iobase + APCI3120_WRITE_MODE_SELECT);
			devpriv->b_EocEosInterrupt = APCI3120_DISABLE;	/* Default settings */
			devpriv->b_InterruptMode = APCI3120_EOC_MODE;
		}

	}
	/* Timer2 interrupt */
	if (int_daq & 0x1) {

		switch (devpriv->b_Timer2Mode) {
		case APCI3120_COUNTER:
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister & APCI3120_DISABLE_EOS_INT;
			outb(devpriv->b_ModeSelectRegister,
				dev->iobase + APCI3120_WRITE_MODE_SELECT);

			s->async->events |= COMEDI_CB_EOA;
			break;

		case APCI3120_TIMER:

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);
			break;

		case APCI3120_WATCHDOG:

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);
			break;

		default:

			/*  disable Timer Interrupt */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister &
				APCI3120_DISABLE_TIMER_INT;

			outb(devpriv->b_ModeSelectRegister,
				dev->iobase + APCI3120_WRITE_MODE_SELECT);

		}

		apci3120_clr_timer2_interrupt(dev);
	}

	if ((int_daq & 0x4) && (devpriv->b_InterruptMode == APCI3120_DMA_MODE)) {
		if (devpriv->ai_running) {

			/* Clear Timer Write TC int */
			outl(APCI3120_CLEAR_WRITE_TC_INT,
			     devpriv->amcc + APCI3120_AMCC_OP_REG_INTCSR);

			apci3120_clr_timer2_interrupt(dev);

			/* do some data transfer */
			apci3120_interrupt_dma(irq, d);
		} else {
			apci3120_timer_enable(dev, 0, false);
			apci3120_timer_enable(dev, 1, false);
		}

	}
	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

/*
 * Configure Timer 2
 *
 * data[0] = TIMER configure as timer
 *	   = WATCHDOG configure as watchdog
 * data[1] = Timer constant
 * data[2] = Timer2 interrupt (1)enable or(0) disable
 */
static int apci3120_config_insn_timer(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_insn *insn,
				      unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned int divisor;

	if (!data[1])
		dev_err(dev->class_dev, "No timer constant!\n");

	devpriv->b_Timer2Interrupt = (unsigned char) data[2];	/*  save info whether to enable or disable interrupt */

	divisor = apci3120_ns_to_timer(dev, 2, data[1], CMDF_ROUND_DOWN);

	apci3120_timer_enable(dev, 2, false);

	/*  Disable TIMER Interrupt */
	devpriv->b_ModeSelectRegister =
		devpriv->
		b_ModeSelectRegister & APCI3120_DISABLE_TIMER_INT & 0xEF;

	/*  Disable Eoc and Eos Interrupts */
	devpriv->b_ModeSelectRegister =
		devpriv->
		b_ModeSelectRegister & APCI3120_DISABLE_EOC_INT &
		APCI3120_DISABLE_EOS_INT;
	outb(devpriv->b_ModeSelectRegister,
	     dev->iobase + APCI3120_WRITE_MODE_SELECT);
	if (data[0] == APCI3120_TIMER) {	/* initialize timer */
		/* Set the Timer 2 in mode 2(Timer) */
		apci3120_timer_set_mode(dev, 2, APCI3120_TIMER_MODE2);

		/* Set timer 2 delay */
		apci3120_timer_write(dev, 2, divisor);

		/*  timer2 in Timer mode enabled */
		devpriv->b_Timer2Mode = APCI3120_TIMER;
	} else {			/*  Initialize Watch dog */
		/* Set the Timer 2 in mode 5(Watchdog) */
		apci3120_timer_set_mode(dev, 2, APCI3120_TIMER_MODE5);

		/* Set timer 2 delay */
		apci3120_timer_write(dev, 2, divisor);

		/* watchdog enabled */
		devpriv->b_Timer2Mode = APCI3120_WATCHDOG;

	}

	return insn->n;

}

/*
 * To start and stop the timer
 *
 * data[0] = 1 (start)
 *	   = 0 (stop)
 *	   = 2 (write new value)
 * data[1] = new value
 *
 * devpriv->b_Timer2Mode = 0 DISABLE
 *			 = 1 Timer
 *			 = 2 Watch dog
 */
static int apci3120_write_insn_timer(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned int divisor;

	if ((devpriv->b_Timer2Mode != APCI3120_WATCHDOG)
		&& (devpriv->b_Timer2Mode != APCI3120_TIMER)) {
		dev_err(dev->class_dev, "timer2 not configured\n");
		return -EINVAL;
	}

	if (data[0] == 2) {	/*  write new value */
		if (devpriv->b_Timer2Mode != APCI3120_TIMER) {
			dev_err(dev->class_dev,
				"timer2 not configured in TIMER MODE\n");
			return -EINVAL;
		}
	}

	switch (data[0]) {
	case APCI3120_START:
		apci3120_clr_timer2_interrupt(dev);

		if (devpriv->b_Timer2Mode == APCI3120_TIMER) {	/* start timer */
			/* Enable Timer */
			devpriv->b_ModeSelectRegister =
				devpriv->b_ModeSelectRegister & 0x0B;
		} else {		/* start watch dog */
			/* Enable WatchDog */
			devpriv->b_ModeSelectRegister =
				(devpriv->
				b_ModeSelectRegister & 0x0B) |
				APCI3120_ENABLE_WATCHDOG;
		}

		/* enable disable interrupt */
		if ((devpriv->b_Timer2Interrupt) == APCI3120_ENABLE) {

			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister |
				APCI3120_ENABLE_TIMER_INT;
			/*  save the task structure to pass info to user */
			devpriv->tsk_Current = current;
		} else {

			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister &
				APCI3120_DISABLE_TIMER_INT;
		}
		outb(devpriv->b_ModeSelectRegister,
		     dev->iobase + APCI3120_WRITE_MODE_SELECT);

		/* start timer */
		if (devpriv->b_Timer2Mode == APCI3120_TIMER)
			apci3120_timer_enable(dev, 2, true);
		break;

	case APCI3120_STOP:
		if (devpriv->b_Timer2Mode == APCI3120_TIMER) {
			/* Disable timer */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister &
				APCI3120_DISABLE_TIMER_COUNTER;
		} else {
			/* Disable WatchDog */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister &
				APCI3120_DISABLE_WATCHDOG;
		}
		/*  Disable timer interrupt */
		devpriv->b_ModeSelectRegister =
			devpriv->
			b_ModeSelectRegister & APCI3120_DISABLE_TIMER_INT;

		/*  Write above states  to register */
		outb(devpriv->b_ModeSelectRegister,
		     dev->iobase + APCI3120_WRITE_MODE_SELECT);

		apci3120_timer_enable(dev, 2, false);

		apci3120_clr_timer2_interrupt(dev);
		break;

	case 2:		/* write new value to Timer */
		if (devpriv->b_Timer2Mode != APCI3120_TIMER) {
			dev_err(dev->class_dev,
				"timer2 not configured in TIMER MODE\n");
			return -EINVAL;
		}

		divisor = apci3120_ns_to_timer(dev, 2, data[1],
					       CMDF_ROUND_DOWN);

		/* Set timer 2 delay */
		apci3120_timer_write(dev, 2, divisor);
		break;
	default:
		return -EINVAL;	/*  Not a valid input */
	}

	return insn->n;
}

/*
 * Read the Timer value
 *
 * for Timer: data[0]= Timer constant
 *
 * for watchdog: data[0] = 0 (still running)
 *			 = 1 (run down)
 */
static int apci3120_read_insn_timer(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned short us_StatusValue;

	if ((devpriv->b_Timer2Mode != APCI3120_WATCHDOG)
		&& (devpriv->b_Timer2Mode != APCI3120_TIMER)) {
		dev_err(dev->class_dev, "timer2 not configured\n");
	}
	if (devpriv->b_Timer2Mode == APCI3120_TIMER) {
		data[0] = apci3120_timer_read(dev, 2);
	} else {			/*  Read watch dog status */

		us_StatusValue = inw(dev->iobase + APCI3120_RD_STATUS);
		us_StatusValue =
			((us_StatusValue & APCI3120_FC_TIMER) >> 12) & 1;
		if (us_StatusValue == 1)
			apci3120_clr_timer2_interrupt(dev);
		data[0] = us_StatusValue;	/*  when data[0] = 1 then the watch dog has rundown */
	}
	return insn->n;
}

static int apci3120_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int val;

	/* the input channels are bits 11:8 of the status reg */
	val = inw(dev->iobase + APCI3120_RD_STATUS);
	data[1] = (val >> 8) & 0xf;

	return insn->n;
}

static int apci3120_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;

	if (comedi_dio_update_state(s, data)) {
		devpriv->do_bits = s->state;
		outb(APCI3120_CTR0_DO_BITS(devpriv->do_bits),
		     dev->iobase + APCI3120_CTR0_REG);
	}

	data[1] = s->state;

	return insn->n;
}

static int apci3120_ao_ready(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + APCI3120_RD_STATUS);
	if (status & 0x0001)	/* waiting for DA_READY */
		return 0;
	return -EBUSY;
}

static int apci3120_ao_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];
		int ret;

		ret = comedi_timeout(dev, s, insn, apci3120_ao_ready, 0);
		if (ret)
			return ret;

		outw(APCI3120_AO_MUX(chan) | APCI3120_AO_DATA(val),
		     dev->iobase + APCI3120_AO_REG(chan));

		s->readback[chan] = val;
	}

	return insn->n;
}
