#include "pci.h"
void init_pci();
uint8_t inb(uint16_t port)
{ 
  uint8_t vl; 
    
  __asm__ volatile ("inb %1, %0\n" : "=a" (vl) : "d" (port));
    
  return vl;
} 
/* put byte value to io port */
void outb(uint16_t port,uint8_t vl)
{
  __asm__ volatile ("outb %0, %1\n" : : "a" (vl), "Nd" (port));
} 
  
/* read 16 bit value from io port */
uint16_t inw(uint16_t port)
{ 
  uint16_t vl;
  
  __asm__ volatile ("inw %1, %0\n" : "=a" (vl) : "d" (port));
    
  return vl;
} 
    
/* put 16 bit value to io port */
void outw(uint16_t port,uint16_t vl)
{   
  __asm__ volatile ("outw %0, %1\n" : : "a" (vl), "Nd" (port));
} 
    
/* read 32 bit value from io port */
uint32_t inl(uint16_t port)
{ 
  uint32_t vl;

  __asm__ volatile ("inl %1, %0\n" : "=a" (vl) : "d" (port));

  return vl;
} 
  
/* put 32 bit value to io port */
void outl(uint16_t port,uint32_t vl)
{
  __asm__ volatile ("outl %0, %1\n" : : "a" (vl), "Nd" (port));
}
static int pci_write(pci_addr_t *d, uint16_t pos, uint8_t len, void *buf)
{
  uint16_t port;
  uint32_t addr;

  if(d->bus > 255 || d->device > 31 || d->function > 5 || pos > 4096) {
    return -1;
  }

//  if(alloc_ioport_range( 0xCF8, 8 ) != 0) return -1;
  port = PCI_CONFIG_DATA + (pos & 3);
  addr = PCI_CONF1_MAKE_ADDRESS(d->bus, d->device, d->function, pos);

  outl(PCI_CONFIG_ADDRESS, addr);


  switch (len) {
  case 1:
    outb(port, ((uint8_t *)buf)[0]);
    break;
  case 2:
    outw(port, ((uint16_t *)buf)[0]);
    break;
  case 4:
    outl(port, ((uint32_t *)buf)[0]);
    break;
  default:
//    free_ioport_range(0xCF8,8);
    return -1;
  }
 // free_ioport_range(0xCF8,8);
  return 0;
}
static int pci_read(pci_addr_t *d, uint16_t pos, uint8_t len, void *buf){

  uint16_t port;
  uint32_t addr;

  if(d->bus > 255 || d->device > 31 || d->function > 5 || pos > 4096) {
    return -1;
  }
//  if(alloc_ioport_range( 0xCF8, 8 ) != 0) return -1;
  port = PCI_CONFIG_DATA + (pos & 3);
  addr = PCI_CONF1_MAKE_ADDRESS(d->bus, d->device, d->function, pos);

  outl(PCI_CONFIG_ADDRESS, addr);

  switch(len) {
  case 1:
    ((uint8_t *)buf)[0] = inb( port );
    break;
  case 2:
    ((uint16_t *)buf)[0] = inw( port );
    break;
  case 4:
    ((uint32_t *)buf)[0] = inl( port );
    break;
  default:
    //free_ioport_range(0xCF8,8);
    return -1;
  }
 // free_ioport_range(0xCF8,8);
  return 0;
}
int pci_generic_read(pci_addr_t *d, uint16_t pos, uint16_t len, void *buf)
{ 
  int c;
  int step;
  
  while(len > 0) {
    if((pos & 1) && len >= 1) {
      step = 1;
    } else if((pos & 3) && len >= 2) {
      step = 2;
    } else if(len < 2) {
      step = 1;
    } else if(len < 4) {
      step = 2;
    } else {
      step = 4;
    }
  
    c = pci_read(d, pos, step, buf);
  
    if( c != 0) {
      return c;
    }
    buf += step;
    pos += step;
    len -= step;
  }

  return 0;
}
static int get_bar(pci_addr_t *addr, int barno, uint32_t *start, uint32_t *len)
{
  int res;
  uint16_t offset;
  uint32_t mask = ~0U;

  // We write all ones to the register and read it.

  offset = PCI_BAR_0 + 4 * barno;

  res = pci_read(addr, offset, 4, start);

  if(res != 0) {
    return -1;
  }

  res = pci_write(addr, offset, 4, &mask);

  if(res != 0) {
    return -1;
  }

  res = pci_read(addr, offset, 4, len);

  if(res != 0) {
    return -1;
  }

  res = pci_write(addr, offset, 4, start);

  if(res != 0) {
    return -1;
  }
	printf("   barno:%d start :%x len:%x \n",barno,*start,*len);
  return 0;
}
static int read_dev_conf(uint8_t bus , uint8_t dev,uint8_t func)
{
  pci_dev_header_t header;
pci_addr_t addr;
  uint32_t start,len;
  int ret;
  int consumed = 0; //number of read BARs
  int produced = 0; //number of filled resources;
  int max_bar;
addr.bus=bus;
addr.device=dev;
addr.function=func;
header.vendor_id=0;
header.device_id=0;
  ret = pci_generic_read(&addr, 0, sizeof(header), &header);

  if(ret != 0) {
    return -1;
  }
  if (header.vendor_id != 0xffff)
  {
   printf(" PCI bus:%d devic:%d func:%d  vendor:%x devices:%x int:%x:%x baser:%x \n",bus,dev,func,header.vendor_id,header.device_id,header.interrupt_line,header.interrupt_pin,header.base_address_registers[0]); 
   get_bar(&addr,0,&start,&len); 
   get_bar(&addr,1,&start,&len); 
   get_bar(&addr,2,&start,&len); 
  }
}
void 
init_pci()
{
	int i;
        printf("Starting the MicroKernel \n");
        //printf(" PCI devices info \n");
        for (i=0; i<5; i++)
                read_dev_conf(0,i,0); 
}
