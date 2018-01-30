#include <linux/timer.h>
#include "br_private.h"
#ifdef CONFIG_BRIDGE_CREDIT_MODE
int add_bca(struct net_bridge *br) {
	struct bridge_credit_allocator *bca;
	
	// 1. memory allocation
	bca = kzalloc(sizeof(struct bridge_credit_allocator), GFP_KERNEL);
	if(bca == NULL) {
		printk(KERN_DEBUG "credit allocator memory error\n");
		return -ENOMEM;
	}

	// 2. assign members inside the target
	bca->br = br;
	INIT_LIST_HEAD(&bca->credit_port_list);
	//spin_lock_init(&bca->credit_port_list_lock);
	bca->total_weight = 0;
	bca->credit_balance = 0;
	bca->credit_port_num = 0;
	setup_timer(&bca->credit_timer, calc_credit, ((unsigned long)(bca)));

	// 3. set other structure related to the target
	br->bca = bca;

	printk(KERN_DEBUG "credit allocator successfully created.\n");
	mod_timer(&bca->credit_timer, jiffies + msecs_to_jiffies(1000));
	return 0;
}

// when removing bridge, just disable bca, the actual deletion of bca is during timer function.
void disable_bca(struct bridge_credit_allocator *bca) {
	bca->br = NULL;
}

// actual deletion of bca
void del_bca(struct bridge_credit_allocator *bca) {
	del_timer(&bca->credit_timer);
	kfree(bca);
	printk(KERN_DEBUG "credit allocator successfully removed.\n");
	return;
}

int br_pay_credit(struct net_bridge_port *p, unsigned int packet_data_size, unsigned int t1, unsigned int t2) {
	// if data_len is zero, then it means no fragment.
	printk(KERN_DEBUG "port_id:%d, len:%d, data_len:%d\n",p->port_no, t1, t2);
	if(p->remaining_credit == ~0U) {
		return BR_CREDIT_PAY_SUCCESS;
	}
	if(p->remaining_credit < packet_data_size) {
		return BR_CREDIT_PAY_FAIL;
	} else {
		p->remaining_credit -= packet_data_size;
		return BR_CREDIT_PAY_SUCCESS;
	}
	return BR_CREDIT_PAY_SUCCESS;
}

void calc_credit(unsigned long bca_pointer) {

	// 0. setting before credit distribution
	struct bridge_credit_allocator *bca = NULL;
	struct net_bridge_port *temp_p, *next_p;
	int total;
	unsigned int weight_left;
	unsigned int credit_left = 0;
	unsigned int credit_total = MAX_CREDIT ;
	unsigned int credit_fair;
	int credit_extra = 0;
	temp_p = next_p = NULL;
	printk(KERN_DEBUG "credit calculation start...\n");
	/* get bca from bca_pointer by type casting
	 * assume that bca is not freed, because there is no way to delete without this function
	 */
	bca = (struct bridge_credit_allocator *)bca_pointer;
	if (bca == NULL) {
		printk(KERN_DEBUG "BCA NULL");
		return;
	}
	if (bca->br == NULL) {
		del_bca(bca);
		return;
	}
	total = bca->total_weight;
	weight_left = total;
	if(list_empty(&bca->credit_port_list) || total == 0)
		goto credit_out;
	// 1. get each credit port and calculate remained_credit
	if(bca->credit_balance > 0)
		credit_total += bca->credit_balance;
	//spin_lock_bh(&bca->credit_port_list_lock);
	list_for_each_entry_safe(temp_p, next_p, &bca->credit_port_list, cp_list) {
		// a. setting fair distribution of credit_total
		weight_left -= temp_p->weight;
		credit_fair = ((credit_total * temp_p->weight) + (total-1) )/ total;
		temp_p->remaining_credit += credit_fair;
		// b. checking given credit
		if(temp_p->min_credit!=0||temp_p->max_credit!=0) {
			if(temp_p->min_credit!=0&&temp_p->remaining_credit < temp_p->min_credit){
				credit_total-= (temp_p->min_credit - temp_p->remaining_credit);
				temp_p->remaining_credit = temp_p->min_credit;
				total-=temp_p->weight;
				list_del(&temp_p->cp_list);
				list_add(&temp_p->cp_list, &bca->credit_port_list);
			}
			if(temp_p->max_credit!=0&&temp_p->remaining_credit > temp_p->max_credit){
				credit_total+= (temp_p->remaining_credit - temp_p->max_credit);
				temp_p->remaining_credit = temp_p->max_credit;
				total-=temp_p->weight;
				list_del(&temp_p->cp_list);
				list_add(&temp_p->cp_list, &bca->credit_port_list);
			}
			goto credit_skip;
		}

		if(temp_p->remaining_credit < MAX_CREDIT) {
			credit_extra = 1;
		} else {
			credit_left += (temp_p->remaining_credit - credit_fair);

			if(weight_left != 0U){
				credit_total += ((credit_left*total)+(weight_left - 1))/weight_left;
				credit_left=0;
			}
			if(credit_extra){
				list_del(&temp_p->cp_list);
				list_add(&temp_p->cp_list, &bca->credit_port_list);
			}
			
			if(bca->credit_port_num > 1)
				temp_p->remaining_credit = credit_fair;
			else
				temp_p->remaining_credit = MAX_CREDIT;
		}
credit_skip:
	printk("credit distribution - port_id:%d, credit:%d\n", temp_p->port_no, temp_p->remaining_credit);
	}
	//spin_unlock_bh(&bca->credit_port_list_lock);
	bca->credit_balance = credit_left;
credit_out:
	// 2. set recursion timer

	mod_timer(&bca->credit_timer, jiffies + msecs_to_jiffies(1000));

	return;
}
#endif
