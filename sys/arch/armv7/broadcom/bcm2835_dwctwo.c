/*	$OpenBSD: octdwctwo.c,v 1.4 2015/02/14 06:46:03 uebayasi Exp $	*/

/*
 * Copyright (c) 2015 Masao Uebayashi <uebayasi@tombiinc.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/pool.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>
#include <armv7/armv7/armv7var.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>
#include <dev/usb/usb_quirks.h>

#include <dev/usb/dwc2/dwc2var.h>
#include <dev/usb/dwc2/dwc2.h>
#include <dev/usb/dwc2/dwc2_core.h>

struct bcm_dwctwo_softc {
	struct dwc2_softc	sc_dwc2;

	void			*sc_ih;
};

int			bcm_dwctwo_match(struct device *, void *, void *);
void			bcm_dwctwo_attach(struct device *, struct device *,
			    void *);
int			bcm_dwctwo_set_dma_addr(void *, dma_addr_t, int);

const struct cfattach bcmdwctwo_ca = {
	sizeof(struct bcm_dwctwo_softc), bcm_dwctwo_match, bcm_dwctwo_attach,
};

struct cfdriver dwctwo_cd = {
	NULL, "dwctwo", DV_DULL
};

static struct dwc2_core_params bcm_dwctwo_params = {
	.otg_cap			= 0,	/* HNP/SRP capable */
	.otg_ver			= 0,	/* 1.3 */
	.dma_enable			= 1,
	.dma_desc_enable		= 0,
	.speed				= 0,	/* High Speed */
	.enable_dynamic_fifo		= 1,
	.en_multiple_tx_fifo		= 1,
	.host_rx_fifo_size		= 774,	/* 774 DWORDs */
	.host_nperio_tx_fifo_size	= 256,	/* 256 DWORDs */
	.host_perio_tx_fifo_size	= 512,	/* 512 DWORDs */
	.max_transfer_size		= 65535,
	.max_packet_count		= 511,
	.host_channels			= 8,
	.phy_type			= 1,	/* UTMI */
	.phy_utmi_width			= 8,	/* 8 bits */
	.phy_ulpi_ddr			= 0,	/* Single */
	.phy_ulpi_ext_vbus		= 0,
	.i2c_enable			= 0,
	.ulpi_fs_ls			= 0,
	.host_support_fs_ls_low_power	= 0,
	.host_ls_low_power_phy_clk	= 0,	/* 48 MHz */
	.ts_dline			= 0,
	.reload_ctl			= 0,
	.ahbcfg				= 0x10,
	.uframe_sched			= 1,
};

int
bcm_dwctwo_match(struct device *parent, void *match, void *aux)
{
	struct armv7_attach_args *aa = aux;

	if (fdt_node_compatible("brcm,bcm2835-usb", aa->aa_node) ||
	    fdt_node_compatible("broadcom,bcm2835-usb", aa->aa_node))
		return (1);

	return 0;
}

void
bcm_dwctwo_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcm_dwctwo_softc *sc = (struct bcm_dwctwo_softc *)self;
	struct armv7_attach_args *aa = aux;
	struct fdt_memory mem;
	int rc;

	printf("\n");

	sc->sc_dwc2.sc_iot = aa->aa_iot;
	sc->sc_dwc2.sc_bus.pipe_size = sizeof(struct usbd_pipe);
	sc->sc_dwc2.sc_bus.dmatag = aa->aa_dmat;
	sc->sc_dwc2.sc_params = &bcm_dwctwo_params;

	rc = fdt_get_memory_address(aa->aa_node, 0, &mem);
	KASSERT(rc == 0);

	rc = bus_space_map(aa->aa_iot, mem.addr, mem.size,
	    0, &sc->sc_dwc2.sc_ioh);
	KASSERT(rc == 0);

	rc = dwc2_init(&sc->sc_dwc2);
	if (rc != 0)
		return;
	sc->sc_dwc2.sc_child = config_found(&sc->sc_dwc2.sc_bus.bdev,
	    &sc->sc_dwc2.sc_bus, usbctlprint);

	sc->sc_ih = arm_intr_establish_fdt(aa->aa_node, IPL_USB, dwc2_intr,
	    (void *)&sc->sc_dwc2, sc->sc_dwc2.sc_bus.bdev.dv_xname);
	KASSERT(sc->sc_ih != NULL);
}
