//#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/kvm_host.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <../../linux-4.12/net/bridge/br_private.h>


#define CONFIG_BRIDGE_CREDIT_MODE
#define MAX_CREDIT 8000000	//kwlee
#define MIN_CREDIT 100000

//for update function
#define PLUS	1
#define MINUS	0

//pay function
#define PAY_SUCCESS	1
#define PAY_FAIL	0

extern void (*fp_newvif)(struct net_bridge_port *p);
extern void (*fp_delvif)(struct net_bridge_port *p);
extern int (*fp_pay)(struct ancs_container *vif, unsigned int packet_data_len);


struct credit_allocator{
	struct list_head active_vif_list;
	spinlock_t active_vif_list_lock;

	struct timer_list account_timer;

	unsigned int total_weight;
	unsigned int credit_balance;
	int num_vif;
};


int pay_credit(struct ancs_container *vif, unsigned int packet_data_size);
void new_vif(struct net_bridge_port *p);
void del_vif(struct net_bridge_port *p);
void update_CA(struct ancs_container *vif, int isplus);
static void credit_accounting(unsigned long data);

//stuff for proc
struct proc_dir_vif{
	char name[10];
	int id;
	struct proc_dir_entry *dir;
	struct proc_dir_entry *file[10];
};

//strdup: const char * to char *
//need to define here because it's not standard c function
char * strdup(const char *str){
	int n = strlen(str) + 1;
	char *dup = kmalloc(n, GFP_KERNEL | __GFP_NOWARN | __GFP_REPEAT);
	if(dup){
		strcpy(dup, str);
	}
	return dup;
}


