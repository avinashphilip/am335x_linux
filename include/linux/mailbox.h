/* mailbox.h */

typedef u32 mbox_msg_t;
struct mailbox;

typedef int __bitwise mailbox_irq_t;
#define IRQ_TX ((__force mailbox_irq_t) 1)
#define IRQ_RX ((__force mailbox_irq_t) 2)

int mailbox_msg_send(struct mailbox *, mbox_msg_t msg);

struct mailbox *mailbox_get(const char *, struct notifier_block *nb);
void mailbox_put(struct mailbox *mbox, struct notifier_block *nb);

void mailbox_save_ctx(struct mailbox *mbox);
void mailbox_restore_ctx(struct mailbox *mbox);
void mailbox_enable_irq(struct mailbox *mbox, mailbox_irq_t irq);
void mailbox_disable_irq(struct mailbox *mbox, mailbox_irq_t irq);
