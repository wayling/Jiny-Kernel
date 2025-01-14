//#define DEBUG_ENABLE 1
#include "common.h"
#include "pci.h"
#include "mm.h"
#include "vfs.h"
#include "task.h"
#include "interface.h"
#include "../virtio.h"
#include "../virtio_ring.h"
#include "../virtio_pci.h"
#include "9p.h"


static queue_t p9_waitq;
virtio_dev_t *p9_dev=0;
void virtio_9p_interrupt(registers_t regs);
static int virtio_addToP9Queue(struct virtqueue *vq, unsigned long buf,
		unsigned long out_len, unsigned long in_len);

static unsigned char vp_get_status(virtio_dev_t *dev) {
	uint16_t addr = dev->pci_ioaddr + VIRTIO_PCI_STATUS;
	return inb(addr);
}
static void vp_set_status(virtio_dev_t *dev, unsigned char status) {
	uint16_t addr = dev->pci_ioaddr + VIRTIO_PCI_STATUS;
	outb(addr, status);
}

int init_virtio_9p_pci(pci_dev_header_t *pci_hdr, virtio_dev_t *dev,uint32_t *msi_vector) {
	unsigned long addr;
	unsigned long features;

	vp_set_status(dev, vp_get_status(dev) + VIRTIO_CONFIG_S_ACKNOWLEDGE);
	DEBUG("Initializing VIRTIO PCI p9 status :%x :  \n",vp_get_status(dev));

	vp_set_status(dev, vp_get_status(dev) + VIRTIO_CONFIG_S_DRIVER);

	addr = dev->pci_ioaddr + VIRTIO_PCI_HOST_FEATURES;
	features = inl(addr);
	DEBUG(" driver Initializing VIRTIO PCI 9P hostfeatures :%x:\n",features);

	virtio_createQueue(0, dev, 2);
	if (*msi_vector > 0){
		*msi_vector =0 ; // disable msi , since p9 is not working MSI
#if 0
		 outw(dev->pci_ioaddr + VIRTIO_MSI_QUEUE_VECTOR,0);
		 outw(dev->pci_ioaddr + VIRTIO_MSI_QUEUE_VECTOR,0xffff);
		 ar_registerInterrupt(msi_vector, virtio_9p_interrupt, "virtio_p9_msi");
#endif
	}
	vp_set_status(dev, vp_get_status(dev) + VIRTIO_CONFIG_S_DRIVER_OK);
	DEBUG(" NEW Initializing.9P INPUT  VIRTIO PCI COMPLETED with driver ok :%x \n",vp_get_status(dev));
	inb(dev->pci_ioaddr + VIRTIO_PCI_ISR);

	sc_register_waitqueue(&p9_waitq,"p9");
	p9_dev = dev;
	p9_initFs();
	return 1;
}
unsigned long p9_write_rpc(p9_client_t *client, const char *fmt, ...) { /* The call will be blocked till the reply is receivied */
	p9_fcall_t pdu;
	int ret,i;
	unsigned long addr;
	va_list ap;
	va_start(ap,fmt);

	p9pdu_init(&pdu, client->type, client->tag, client,client->pkt_buf, client->pkt_len);
	ret = p9pdu_write(&pdu, fmt, ap);
	va_end(ap);
	p9pdu_finalize(&pdu);

	struct scatterlist sg[4];
	unsigned int out, in;
	sg[0].page_link = client->pkt_buf;
	sg[0].length = 1024;
	sg[0].offset = 0;
	out = 1;
	if (client->type == P9_TYPE_TREAD) {
		sg[1].page_link = client->pkt_buf + 1024;
		sg[1].length = 11; /* exactly 11 bytes for read response header , data will be from user buffer*/
		sg[1].offset = 0;
		sg[2].page_link = client->user_data;
		sg[2].length = client->userdata_len;
		sg[2].offset = 0;
		in = 2;
	} else  if  (client->type == P9_TYPE_TWRITE) {
		sg[1].page_link = client->user_data;
		sg[1].length = client->userdata_len;
		sg[1].offset = 0;
		sg[0].length =23 ; /* this for header , eventhough it is having space pick the data from sg[1] */

		sg[2].page_link = client->pkt_buf + 1024;
		sg[2].length = 1024;
		sg[2].offset = 0;
		out = 2;
		in =1;
	} else {
		sg[1].page_link = client->pkt_buf + 1024;
		sg[1].length = 1024;
		sg[1].offset = 0;
		in = 1;
	}
	virtqueue_enable_cb(p9_dev->vq[0]);
	virtqueue_add_buf_gfp(p9_dev->vq[0], sg, out, in, sg[0].page_link, 0);
	virtqueue_kick(p9_dev->vq[0]);

	sc_wait(&p9_waitq, 50);
	unsigned int len;
	len = 0;
	i=0;
	addr=0;
	while (i < 3 && addr == 0) {
		addr = virtio_removeFromQueue(p9_dev->vq[0], &len); /* TODO : here sometime returns zero because of some race condition, the packet is not recevied */
		i++;
		if (addr == 0) {
			ut_printf(" RACE CONDITION in P9 so sleeping for while \n");
			sc_sleep(300);
		}
	}
	if (addr != client->pkt_buf) {
		DEBUG("9p write : got invalid address : %x \n",addr);
		return 0;
	}
	return client->pkt_buf;
}

int p9_read_rpc(p9_client_t *client, const char *fmt, ...) {
	unsigned char *recv;
	p9_fcall_t pdu;
	int ret;
	uint32_t total_len;
	unsigned char type;
	uint16_t tag;

	va_list ap;
	va_start(ap,fmt);

	recv = client->pkt_buf + 1024;
	p9pdu_init(&pdu, 0, 0, client,recv, 1024);
	ret = p9pdu_read_v(&pdu, "dbw", &total_len, &type, &tag);
	client->recv_type = type;
	ret = p9pdu_read(&pdu, fmt, ap);
	va_end(ap);
	DEBUG("Recv Header ret:%x total len :%x stype:%x(%d) rtype:%x(%d) tag:%x \n",ret ,total_len,client->type,client->type,type,type,tag);
	if (type == 107) { // TODO better way of handling other and this error
		recv[100]='\0';
		DEBUG(" recv error data :%s: \n ",&recv[9]);
	}
	return ret;
}

void virtio_9p_interrupt(registers_t regs) { // TODO: handling similar  type of interrupt generating while serving P9 interrupt.
	unsigned char isr;
	int ret;

	if (p9_dev->msi==0)
	  isr = inb(p9_dev->pci_ioaddr + VIRTIO_PCI_ISR);
	ret = sc_wakeUp(&p9_waitq); /* wake all the waiting processes */
}
