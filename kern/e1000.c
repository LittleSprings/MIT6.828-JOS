#include <kern/e1000.h>
#include <kern/pmap.h>
#include <kern/pci.h>
#include <kern/pcireg.h>
#include <inc/string.h>

#define E1000_LOCATE(offset)  (offset >> 2)

volatile uint32_t *e1000;
/* 为描述符列表分配静态内存 */
struct E1000TxDesc tx_desc_list[TX_DESC_SIZE] __attribute__((aligned (PGSIZE))) ;
char tx_pbuf[TX_DESC_SIZE][TX_PACKET_SIZE] __attribute__((aligned (PGSIZE))) ;

struct E1000RxDesc rx_desc_list[RX_DESC_SIZE] __attribute__((aligned (PGSIZE))) ;
char rx_pbuf[RX_DESC_SIZE][RX_PACKET_SIZE] __attribute__((aligned (PGSIZE))) ;

// LAB 6: Your driver code here

int
pci_e1000_attach(struct pci_func * pcif) 
{
    pci_func_enable(pcif);          //在使能的时候，内核与e1000协商一个mmio区域，方便读写设备，并将mimo的基址和大小存储在reg_base[0]和reg_size[0]中（）
    // 该值是物理值，即将内存的物理地址和设备寄存器绑定，以后读写这个物理地址就能实现读写设备，当然，系统并不能直接操作物理地址，因此需要映射到虚拟内存中。
    e1000 = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    e1000_transmit_init();
    e1000_receive_init();
    // e1000_transmit(e1000, 0x2a);
    // uint32_t *status_reg = (uint32_t *)E1000_REG(E1000_STATUS);
    cprintf("e1000[E1000_LOCATE(E1000_STATUS)] = %08x\n", e1000[E1000_LOCATE(E1000_STATUS)]);
    // cprintf("device status:[%08x]\n", *(uint32_t *)((uint8_t *)bar_va + E1000_STATUS));
    return 0;
}

void e1000_transmit_init(void) 
{
   int i;
   // 初始化tx_desc_array数组
   for (i = 0; i < TX_DESC_SIZE; i++) {
		tx_desc_list[i].addr = PADDR(tx_pbuf[i]);
		tx_desc_list[i].status = E1000_TXD_STAT_DD;
		tx_desc_list[i].cmd    =  E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
   }

    e1000[E1000_LOCATE(E1000_TDBAL)] = PADDR(tx_desc_list);
    e1000[E1000_LOCATE(E1000_TDBAH)] = 0;
    e1000[E1000_LOCATE(E1000_TDLEN)] = sizeof(struct E1000TxDesc) * TX_DESC_SIZE;
    // ensure that TDH and TDT are 0 index not offset
    e1000[E1000_LOCATE(E1000_TDH)] = 0;
    e1000[E1000_LOCATE(E1000_TDT)] = 0;

    // Initialize the Transmit Control Register (TCTL)
    e1000[E1000_LOCATE(E1000_TCTL)] = E1000_TCTL_EN | 
                                      E1000_TCTL_PSP |
                                      (E1000_TCTL_CT & (0x10 << 4)) |
                                      (E1000_TCTL_COLD & (0x40 << 12));

    // 10 8 6 
    // 10 8 12
    e1000[E1000_LOCATE(E1000_TIPG)] = 10 | (8 << 10) | (12 << 20);
}

int 
e1000_transmit(void *addr, size_t len)
{
    
  size_t tdt = e1000[E1000_LOCATE(E1000_TDT)];
    struct E1000TxDesc *tail_desc = &tx_desc_list[tdt];
    if ( !(tail_desc->status & E1000_TXD_STAT_DD )) {
        // Status is not DD
        return -1;
    }
    memmove(tx_pbuf[tdt], addr, len);
    
    tail_desc->length = (uint16_t )len;
    // clear DD 
    tail_desc->status &= (~E1000_TXD_STAT_DD);

    e1000[E1000_LOCATE(E1000_TDT)] = (tdt+1) % TX_DESC_SIZE;
    return 0;
    
}

uint32_t E1000_MAC[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};


void 
e1000_set_mac_addr(uint32_t mac[])
{
    uint32_t low = 0, high = 0;
    int i;

    for (i = 0; i < 4; i++) {
        low |= mac[i] << (8 * i);
    }

    for (i = 4; i < 6; i++) {
        high |= mac[i] << (8 * i);
    }

    e1000[E1000_LOCATE(E1000_RA)] = low;
    e1000[E1000_LOCATE(E1000_RA) + 1] = high | E1000_RAH_AV;
}

void 
e1000_receive_init()
{
    size_t i;
    memset(rx_desc_list, 0 , sizeof(struct E1000RxDesc) * RX_DESC_SIZE);
    for (i = 0; i < RX_DESC_SIZE; i++) {
        rx_desc_list[i].addr = PADDR(rx_pbuf[i]);
    }
    
    // cprintf("mac addr %x:%x", e1000[E1000_LOCATE(E1000_RA)], e1000[E1000_LOCATE(E1000_RA) + 1]  );
    e1000[E1000_LOCATE(E1000_ICS)] = 0;
    e1000[E1000_LOCATE(E1000_IMS)] = 0;
    e1000[E1000_LOCATE(E1000_RDBAL)] = PADDR(rx_desc_list);
    e1000[E1000_LOCATE(E1000_RDBAH)] = 0;

    e1000[E1000_LOCATE(E1000_RDLEN)] = sizeof(struct E1000RxDesc) * RX_DESC_SIZE;
    e1000[E1000_LOCATE(E1000_RDT)] = RX_DESC_SIZE - 1;
    // 写了两遍 RDH，查了好久的BUG。
    e1000[E1000_LOCATE(E1000_RDH)] = 0;

    e1000[E1000_LOCATE(E1000_RCTL)] = E1000_RCTL_EN | E1000_RCTL_SECRC | E1000_RCTL_BAM | E1000_RCTL_SZ_2048;

    e1000_set_mac_addr(E1000_MAC);
}

int e1000_receive(void *buf, size_t len)
{
    static size_t next = 0;
    size_t tail = e1000[E1000_LOCATE(E1000_RDT)];
    // cprintf("e1000_receive start\n");
    if ( !(rx_desc_list[next].status & E1000_RXD_STAT_DD) ) {
        // cprintf("no packet\n");
        return -1;
    }
    len = rx_desc_list[next].length;
    memcpy(buf, rx_pbuf[next], len);

    
    rx_desc_list[next].status &= ~E1000_RXD_STAT_DD; 
    next = (next + 1) % RX_DESC_SIZE;
    e1000[E1000_LOCATE(E1000_RDT)] = (tail + 1 ) % RX_DESC_SIZE;
    cprintf("e1000_receive return 0\n");
    return 0;
}