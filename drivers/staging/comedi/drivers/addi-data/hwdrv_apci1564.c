/* Digital Input IRQ Function Selection */
#define APCI1564_DI_INT_OR				(0 << 1)
#define APCI1564_DI_INT_AND				(1 << 1)

/* Digital Input Interrupt Enable Disable. */
#define APCI1564_DI_INT_ENABLE				0x4
#define APCI1564_DI_INT_DISABLE				0xfffffffb

/* Digital Output Interrupt Enable Disable. */
#define APCI1564_DO_VCC_INT_ENABLE			0x1
#define APCI1564_DO_VCC_INT_DISABLE			0xfffffffe
#define APCI1564_DO_CC_INT_ENABLE			0x2
#define APCI1564_DO_CC_INT_DISABLE			0xfffffffd

/* TIMER COUNTER WATCHDOG DEFINES */
#define ADDIDATA_TIMER					0
#define ADDIDATA_COUNTER				1
#define ADDIDATA_WATCHDOG				2

/*
 * devpriv->counters Register Map
 */
#define APCI1564_COUNTER_REG(x)				(0x00 + ((x) * 0x20))
#define APCI1564_COUNTER_RELOAD_REG(x)			(0x04 + ((x) * 0x20))
#define APCI1564_COUNTER_TIMEBASE_REG(x)		(0x08 + ((x) * 0x20))
#define APCI1564_COUNTER_CTRL_REG(x)			(0x0c + ((x) * 0x20))
#define APCI1564_COUNTER_STATUS_REG(x)			(0x10 + ((x) * 0x20))
#define APCI1564_COUNTER_IRQ_REG(x)			(0x14 + ((x) * 0x20))
#define APCI1564_COUNTER_WARN_TIMEVAL_REG(x)		(0x18 + ((x) * 0x20))
#define APCI1564_COUNTER_WARN_TIMEBASE_REG(x)		(0x1c + ((x) * 0x20))

/*
 * Configures The Timer or Counter
 *
 * data[0] Configure as: 0 = Timer, 1 = Counter
 * data[1] 1 = Enable Interrupt, 0 = Disable Interrupt
 * data[2] Time Unit
 * data[3] Reload Value
 * data[4] Timer Mode
 * data[5] Timer Counter Watchdog Number
 * data[6] Counter Direction
 */
static int apci1564_timer_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int ul_Command1 = 0;

	devpriv->tsk_current = current;
	if (data[0] == ADDIDATA_TIMER) {
		/* First Stop The Timer */
		ul_Command1 = inl(devpriv->timer + APCI1564_TIMER_CTRL_REG);
		ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
		/* Stop The Timer */
		outl(ul_Command1, devpriv->timer + APCI1564_TIMER_CTRL_REG);

		devpriv->timer_select_mode = ADDIDATA_TIMER;
		if (data[1] == 1) {
			/* Enable TIMER int & DISABLE ALL THE OTHER int SOURCES */
			outl(0x02, devpriv->timer + APCI1564_TIMER_CTRL_REG);
			outl(0x0, dev->iobase + APCI1564_DI_IRQ_REG);
			outl(0x0, dev->iobase + APCI1564_DO_IRQ_REG);
			outl(0x0, dev->iobase + APCI1564_WDOG_IRQ_REG);
			outl(0x0,
			     devpriv->counters + APCI1564_COUNTER_IRQ_REG(0));
			outl(0x0,
			     devpriv->counters + APCI1564_COUNTER_IRQ_REG(1));
			outl(0x0,
			     devpriv->counters + APCI1564_COUNTER_IRQ_REG(2));
		} else {
			/* disable Timer interrupt */
			outl(0x0, devpriv->timer + APCI1564_TIMER_CTRL_REG);
		}

		/*  Loading Timebase */
		outl(data[2], devpriv->timer + APCI1564_TIMER_TIMEBASE_REG);

		/* Loading the Reload value */
		outl(data[3], devpriv->timer + APCI1564_TIMER_RELOAD_REG);

		ul_Command1 = inl(devpriv->timer + APCI1564_TIMER_CTRL_REG);
		ul_Command1 = (ul_Command1 & 0xFFF719E2UL) | 2UL << 13UL | 0x10UL;
		/* mode 2 */
		outl(ul_Command1, devpriv->timer + APCI1564_TIMER_CTRL_REG);
	} else if (data[0] == ADDIDATA_COUNTER) {
		devpriv->timer_select_mode = ADDIDATA_COUNTER;

		/* First Stop The Counter */
		ul_Command1 = inl(devpriv->counters +
				  APCI1564_COUNTER_CTRL_REG(chan));
		ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
		/* Stop The Timer */
		outl(ul_Command1,
		     devpriv->counters + APCI1564_COUNTER_CTRL_REG(chan));

		/* Set the reload value */
		outl(data[3],
		     devpriv->counters + APCI1564_COUNTER_RELOAD_REG(chan));

		/* Set the mode :             */
		/* - Disable the hardware     */
		/* - Disable the counter mode */
		/* - Disable the warning      */
		/* - Disable the reset        */
		/* - Disable the timer mode   */
		/* - Enable the counter mode  */

		ul_Command1 =
			(ul_Command1 & 0xFFFC19E2UL) | 0x80000UL |
			(unsigned int) ((unsigned int) data[4] << 16UL);
		outl(ul_Command1,
		     devpriv->counters + APCI1564_COUNTER_CTRL_REG(chan));

		/*  Enable or Disable Interrupt */
		ul_Command1 = (ul_Command1 & 0xFFFFF9FD) | (data[1] << 1);
		outl(ul_Command1,
		     devpriv->counters + APCI1564_COUNTER_CTRL_REG(chan));

		/* Set the Up/Down selection */
		ul_Command1 = (ul_Command1 & 0xFFFBF9FFUL) | (data[6] << 18);
		outl(ul_Command1,
		     devpriv->counters + APCI1564_COUNTER_CTRL_REG(chan));
	} else {
		dev_err(dev->class_dev, "Invalid subdevice.\n");
	}
	return insn->n;
}

/*
 * Start / Stop The Selected Timer or Counter
 *
 * data[0] Configure as: 0 = Timer, 1 = Counter
 * data[1] 0 = Stop, 1 = Start, 2 = Trigger Clear (Only Counter)
 */
static int apci1564_timer_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int ul_Command1 = 0;

	if (devpriv->timer_select_mode == ADDIDATA_TIMER) {
		if (data[1] == 1) {
			ul_Command1 = inl(devpriv->timer +
					  APCI1564_TIMER_CTRL_REG);
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x1UL;

			/* Enable the Timer */
			outl(ul_Command1,
			     devpriv->timer + APCI1564_TIMER_CTRL_REG);
		} else if (data[1] == 0) {
			/* Stop The Timer */

			ul_Command1 = inl(devpriv->timer +
					  APCI1564_TIMER_CTRL_REG);
			ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
			outl(ul_Command1,
			     devpriv->timer + APCI1564_TIMER_CTRL_REG);
		}
	} else if (devpriv->timer_select_mode == ADDIDATA_COUNTER) {
		ul_Command1 = inl(devpriv->counters +
				  APCI1564_COUNTER_CTRL_REG(chan));
		if (data[1] == 1) {
			/* Start the Counter subdevice */
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x1UL;
		} else if (data[1] == 0) {
			/*  Stops the Counter subdevice */
			ul_Command1 = 0;

		} else if (data[1] == 2) {
			/*  Clears the Counter subdevice */
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x400;
		}
		outl(ul_Command1,
		     devpriv->counters + APCI1564_COUNTER_CTRL_REG(chan));
	} else {
		dev_err(dev->class_dev, "Invalid subdevice.\n");
	}
	return insn->n;
}

/*
 * Read The Selected Timer or Counter
 */
static int apci1564_timer_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int ul_Command1 = 0;

	if (devpriv->timer_select_mode == ADDIDATA_TIMER) {
		/*  Stores the status of the Timer */
		data[0] = inl(devpriv->timer + APCI1564_TIMER_STATUS_REG) & 0x1;

		/*  Stores the Actual value of the Timer */
		data[1] = inl(devpriv->timer + APCI1564_TIMER_REG);
	} else if (devpriv->timer_select_mode == ADDIDATA_COUNTER) {
		/*  Read the Counter Actual Value. */
		data[0] = inl(devpriv->counters +
			      APCI1564_COUNTER_REG(chan));
		ul_Command1 = inl(devpriv->counters +
				  APCI1564_COUNTER_STATUS_REG(chan));

		/* Get the software trigger status */
		data[1] = (unsigned char) ((ul_Command1 >> 1) & 1);

		/* Get the hardware trigger status */
		data[2] = (unsigned char) ((ul_Command1 >> 2) & 1);

		/* Get the software clear status */
		data[3] = (unsigned char) ((ul_Command1 >> 3) & 1);

		/* Get the overflow status */
		data[4] = (unsigned char) ((ul_Command1 >> 0) & 1);
	} else {
		dev_err(dev->class_dev, "Invalid subdevice.\n");
	}
	return insn->n;
}
