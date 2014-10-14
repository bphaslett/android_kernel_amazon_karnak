#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#define ADDIDATA_WATCHDOG 2	/*  Or shold it be something else */

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci035.c"
#include "addi-data/addi_common.c"

static const struct addi_board apci035_boardtypes[] = {
	{
		.pc_DriverName		= "apci035",
		.i_IorangeBase1		= APCI035_ADDRESS_RANGE,
		.i_PCIEeprom		= 1,
		.pc_EepromChip		= "S5920",
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0xff,
		.pr_AiRangelist		= &range_apci035_ai,
		.i_Timer		= 1,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= apci035_interrupt,
		.ai_config		= apci035_ai_config,
		.ai_read		= apci035_ai_read,
		.timer_config		= apci035_timer_config,
		.timer_write		= apci035_timer_write,
		.timer_read		= apci035_timer_read,
	},
};

static int apci035_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct addi_board *this_board = dev->board_ptr;
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	unsigned int dw_Dummy;
	int ret;

	dev->board_ptr = &apci035_boardtypes[0];
	dev->board_name = this_board->pc_DriverName;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	if (this_board->i_IorangeBase1)
		dev->iobase = pci_resource_start(pcidev, 1);
	else
		dev->iobase = pci_resource_start(pcidev, 0);

	devpriv->iobase = dev->iobase;
	devpriv->i_IobaseAmcc = pci_resource_start(pcidev, 0);
	devpriv->i_IobaseAddon = pci_resource_start(pcidev, 2);
	devpriv->i_IobaseReserved = pci_resource_start(pcidev, 3);

	/* Initialize parameters that can be overridden in EEPROM */
	devpriv->s_EeParameters.i_NbrAiChannel = this_board->i_NbrAiChannel;
	devpriv->s_EeParameters.i_NbrAoChannel = this_board->i_NbrAoChannel;
	devpriv->s_EeParameters.i_AiMaxdata = this_board->i_AiMaxdata;
	devpriv->s_EeParameters.i_AoMaxdata = this_board->i_AoMaxdata;
	devpriv->s_EeParameters.i_NbrDiChannel = this_board->i_NbrDiChannel;
	devpriv->s_EeParameters.i_NbrDoChannel = this_board->i_NbrDoChannel;
	devpriv->s_EeParameters.i_DoMaxdata = this_board->i_DoMaxdata;
	devpriv->s_EeParameters.i_Timer = this_board->i_Timer;
	devpriv->s_EeParameters.ui_MinAcquisitiontimeNs =
		this_board->ui_MinAcquisitiontimeNs;
	devpriv->s_EeParameters.ui_MinDelaytimeNs =
		this_board->ui_MinDelaytimeNs;

	/* ## */

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, v_ADDI_Interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	/*  Read eepeom and fill addi_board Structure */

	if (this_board->i_PCIEeprom) {
		if (!(strcmp(this_board->pc_EepromChip, "S5920"))) {
			/*  Set 3 wait stait */
			if (!(strcmp(dev->board_name, "apci035")))
				outl(0x80808082, devpriv->i_IobaseAmcc + 0x60);
			else
				outl(0x83838383, devpriv->i_IobaseAmcc + 0x60);

			/*  Enable the interrupt for the controller */
			dw_Dummy = inl(devpriv->i_IobaseAmcc + 0x38);
			outl(dw_Dummy | 0x2000, devpriv->i_IobaseAmcc + 0x38);
		}
		addi_eeprom_read_info(dev, pci_resource_start(pcidev, 0));
	}

	ret = comedi_alloc_subdevices(dev, 7);
	if (ret)
		return ret;

	/*  Allocate and Initialise AI Subdevice Structures */
	s = &dev->subdevices[0];
	if ((devpriv->s_EeParameters.i_NbrAiChannel)
		|| (this_board->i_NbrAiChannelDiff)) {
		dev->read_subdev = s;
		s->type = COMEDI_SUBD_AI;
		s->subdev_flags =
			SDF_READABLE | SDF_COMMON | SDF_GROUND
			| SDF_DIFF;
		if (devpriv->s_EeParameters.i_NbrAiChannel)
			s->n_chan = devpriv->s_EeParameters.i_NbrAiChannel;
		else
			s->n_chan = this_board->i_NbrAiChannelDiff;
		s->maxdata = devpriv->s_EeParameters.i_AiMaxdata;
		s->len_chanlist = this_board->i_AiChannelList;
		s->range_table = this_board->pr_AiRangelist;

		s->insn_config = this_board->ai_config;
		s->insn_read = this_board->ai_read;
		s->insn_write = this_board->ai_write;
		s->insn_bits = this_board->ai_bits;
		s->do_cmdtest = this_board->ai_cmdtest;
		s->do_cmd = this_board->ai_cmd;
		s->cancel = this_board->ai_cancel;

	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise AO Subdevice Structures */
	s = &dev->subdevices[1];
	if (devpriv->s_EeParameters.i_NbrAoChannel) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = devpriv->s_EeParameters.i_NbrAoChannel;
		s->maxdata = devpriv->s_EeParameters.i_AoMaxdata;
		s->len_chanlist =
			devpriv->s_EeParameters.i_NbrAoChannel;
		s->insn_write = this_board->ao_write;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}
	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[2];
	if (devpriv->s_EeParameters.i_NbrDiChannel) {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = devpriv->s_EeParameters.i_NbrDiChannel;
		s->maxdata = 1;
		s->len_chanlist =
			devpriv->s_EeParameters.i_NbrDiChannel;
		s->range_table = &range_digital;
		s->insn_config = this_board->di_config;
		s->insn_read = this_board->di_read;
		s->insn_write = this_board->di_write;
		s->insn_bits = this_board->di_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}
	/*  Allocate and Initialise DO Subdevice Structures */
	s = &dev->subdevices[3];
	if (devpriv->s_EeParameters.i_NbrDoChannel) {
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags =
			SDF_READABLE | SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = devpriv->s_EeParameters.i_NbrDoChannel;
		s->maxdata = devpriv->s_EeParameters.i_DoMaxdata;
		s->len_chanlist =
			devpriv->s_EeParameters.i_NbrDoChannel;
		s->range_table = &range_digital;

		/* insn_config - for digital output memory */
		s->insn_config = this_board->do_config;
		s->insn_write = this_board->do_write;
		s->insn_bits = this_board->do_bits;
		s->insn_read = this_board->do_read;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[4];
	if (devpriv->s_EeParameters.i_Timer) {
		s->type = COMEDI_SUBD_TIMER;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = 1;
		s->maxdata = 0;
		s->len_chanlist = 1;
		s->range_table = &range_digital;

		s->insn_write = this_board->timer_write;
		s->insn_read = this_board->timer_read;
		s->insn_config = this_board->timer_config;
		s->insn_bits = this_board->timer_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise TTL */
	s = &dev->subdevices[5];
	s->type = COMEDI_SUBD_UNUSED;

	/* EEPROM */
	s = &dev->subdevices[6];
	if (this_board->i_PCIEeprom) {
		s->type = COMEDI_SUBD_MEMORY;
		s->subdev_flags = SDF_READABLE | SDF_INTERNAL;
		s->n_chan = 256;
		s->maxdata = 0xffff;
		s->insn_read = i_ADDIDATA_InsnReadEeprom;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	apci035_reset(dev);

	return 0;
}

static void apci035_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		apci035_reset(dev);
	comedi_pci_detach(dev);
}

static struct comedi_driver apci035_driver = {
	.driver_name	= "addi_apci_035",
	.module		= THIS_MODULE,
	.auto_attach	= apci035_auto_attach,
	.detach		= apci035_detach,
};

static int apci035_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci035_driver, id->driver_data);
}

static const struct pci_device_id apci035_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA,  0x0300) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci035_pci_table);

static struct pci_driver apci035_pci_driver = {
	.name		= "addi_apci_035",
	.id_table	= apci035_pci_table,
	.probe		= apci035_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci035_driver, apci035_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
