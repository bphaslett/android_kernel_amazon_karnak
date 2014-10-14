#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/hwdrv_apci3120.c"

enum apci3120_boardid {
	BOARD_APCI3120,
	BOARD_APCI3001,
};

struct apci3120_board {
	const char *name;
	int i_AiChannelList;
	int i_NbrAoChannel;
	int i_AiMaxdata;
	int i_AoMaxdata;
};

static const struct apci3120_board apci3120_boardtypes[] = {
	[BOARD_APCI3120] = {
		.name			= "apci3120",
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 8,
		.i_AiMaxdata		= 0xffff,
		.i_AoMaxdata		= 0x3fff,
	},
	[BOARD_APCI3001] = {
		.name			= "apci3001",
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0xfff,
	},
};

static int apci3120_auto_attach(struct comedi_device *dev,
				unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct apci3120_board *this_board = NULL;
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret, order, i;

	if (context < ARRAY_SIZE(apci3120_boardtypes))
		this_board = &apci3120_boardtypes[context];
	if (!this_board)
		return -ENODEV;
	dev->board_ptr = this_board;
	dev->board_name = this_board->name;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	pci_set_master(pcidev);

	dev->iobase = pci_resource_start(pcidev, 1);
	devpriv->iobase = dev->iobase;
	devpriv->i_IobaseAmcc = pci_resource_start(pcidev, 0);
	devpriv->i_IobaseAddon = pci_resource_start(pcidev, 2);
	devpriv->i_IobaseReserved = pci_resource_start(pcidev, 3);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci3120_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	/* Allocate DMA buffers */
	for (i = 0; i < 2; i++) {
		for (order = 2; order >= 0; order--) {
			devpriv->ul_DmaBufferVirtual[i] =
			    dma_alloc_coherent(dev->hw_dev, PAGE_SIZE << order,
					       &devpriv->ul_DmaBufferHw[i],
					       GFP_KERNEL);

			if (devpriv->ul_DmaBufferVirtual[i])
				break;
		}
		if (!devpriv->ul_DmaBufferVirtual[i])
			break;
		devpriv->ui_DmaBufferSize[i] = PAGE_SIZE << order;
	}
	if (devpriv->ul_DmaBufferVirtual[0])
		devpriv->us_UseDma = 1;

	if (devpriv->ul_DmaBufferVirtual[1])
		devpriv->b_DmaDoubleBuffer = 1;

	ret = comedi_alloc_subdevices(dev, 5);
	if (ret)
		return ret;

	/*  Allocate and Initialise AI Subdevice Structures */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags =
		SDF_READABLE | SDF_COMMON | SDF_GROUND
		| SDF_DIFF;
	s->n_chan = 16;
	s->maxdata = this_board->i_AiMaxdata;
	s->len_chanlist = this_board->i_AiChannelList;
	s->range_table = &range_apci3120_ai;

	s->insn_config = apci3120_ai_insn_config;
	s->insn_read = apci3120_ai_insn_read;
	s->do_cmdtest = apci3120_ai_cmdtest;
	s->do_cmd = apci3120_ai_cmd;
	s->cancel = apci3120_cancel;

	/*  Allocate and Initialise AO Subdevice Structures */
	s = &dev->subdevices[1];
	if (this_board->i_NbrAoChannel) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = this_board->i_NbrAoChannel;
		s->maxdata = this_board->i_AoMaxdata;
		s->len_chanlist = this_board->i_NbrAoChannel;
		s->range_table = &range_apci3120_ao;
		s->insn_write = apci3120_ao_insn_write;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[2];
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 4;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = apci3120_di_insn_bits;

	/*  Allocate and Initialise DO Subdevice Structures */
	s = &dev->subdevices[3];
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags =
		SDF_READABLE | SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 4;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = apci3120_do_insn_bits;

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[4];
	s->type = COMEDI_SUBD_TIMER;
	s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 1;
	s->maxdata = 0;
	s->len_chanlist = 1;
	s->range_table = &range_digital;

	s->insn_write = apci3120_write_insn_timer;
	s->insn_read = apci3120_read_insn_timer;
	s->insn_config = apci3120_config_insn_timer;

	apci3120_reset(dev);
	return 0;
}

static void apci3120_detach(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;

	if (dev->iobase)
		apci3120_reset(dev);
	comedi_pci_detach(dev);
	if (devpriv) {
		unsigned int i;

		for (i = 0; i < 2; i++) {
			if (devpriv->ul_DmaBufferVirtual[i]) {
				dma_free_coherent(dev->hw_dev,
						  devpriv->ui_DmaBufferSize[i],
						  devpriv->
						  ul_DmaBufferVirtual[i],
						  devpriv->ul_DmaBufferHw[i]);
			}
		}
	}
}

static struct comedi_driver apci3120_driver = {
	.driver_name	= "addi_apci_3120",
	.module		= THIS_MODULE,
	.auto_attach	= apci3120_auto_attach,
	.detach		= apci3120_detach,
};

static int apci3120_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci3120_driver, id->driver_data);
}

static const struct pci_device_id apci3120_pci_table[] = {
	{ PCI_VDEVICE(AMCC, 0x818d), BOARD_APCI3120 },
	{ PCI_VDEVICE(AMCC, 0x828d), BOARD_APCI3001 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci3120_pci_table);

static struct pci_driver apci3120_pci_driver = {
	.name		= "addi_apci_3120",
	.id_table	= apci3120_pci_table,
	.probe		= apci3120_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci3120_driver, apci3120_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("ADDI-DATA APCI-3120, Analog input board");
MODULE_LICENSE("GPL");
