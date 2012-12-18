/* mailbox.h */

#ifndef MAILBOX_H
#define MAILBOX_H

#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kfifo.h>
#include <linux/mailbox.h>

typedef int __bitwise mailbox_type_t;
#define MBOX_HW_FIFO1_TYPE ((__force mailbox_type_t) 1)
#define MBOX_HW_FIFO2_TYPE ((__force mailbox_type_t) 2)

struct mailbox_ops {
	mailbox_type_t        type;
	int             (*startup)(struct mailbox *mbox);
	void            (*shutdown)(struct mailbox *mbox);
	/* fifo */
	mbox_msg_t      (*fifo_read)(struct mailbox *mbox, struct mailbox_msg *msg);
	int             (*fifo_write)(struct mailbox *mbox, struct mailbox_msg *msg);
	int             (*fifo_empty)(struct mailbox *mbox);
	int             (*fifo_full)(struct mailbox *mbox);
	/* irq */
	void            (*enable_irq)(struct mailbox *mbox,
			mailbox_irq_t irq);
	void            (*disable_irq)(struct mailbox *mbox,
			mailbox_irq_t irq);
	void            (*ack_irq)(struct mailbox *mbox, mailbox_irq_t irq);
	int             (*is_irq)(struct mailbox *mbox, mailbox_irq_t irq);
	/* ctx */
	void            (*save_ctx)(struct mailbox *mbox);
	void            (*restore_ctx)(struct mailbox *mbox);
};

struct mailbox_queue {
	spinlock_t              lock;
	struct kfifo            fifo;
	struct work_struct      work;
	struct tasklet_struct   tasklet;
	struct mailbox          *mbox;
	bool full;
};

struct mailbox {
	char                    *name;
	unsigned int            irq;
	struct mailbox_queue  *txq, *rxq;
	struct mailbox_ops    *ops;
	struct device           *dev;
	void                    *priv;
	int                     use_count;
	struct blocking_notifier_head   notifier;
};

void mailbox_init_seq(struct mailbox *);

int mailbox_register(struct device *parent, struct mailbox **);
int mailbox_unregister(void);

#endif /* MAILBOX_H */
