#include "common.h"

extern struct list_head off_list;
struct credit_allocator *CA;
LIST_HEAD(active_vif_list);
//id for vif
int vif_cnt, counter, reset;

static struct proc_dir_entry *proc_root_dir;
static struct proc_dir_vif proc_vif[64];
int fileread = 0;

static void credit_accounting(unsigned long data){
	struct ancs_container *temp_vif, *next_vif;
	int total = CA->total_weight;
	unsigned int weight_left=total;
	unsigned int credit_left = 0;
	unsigned int credit_total = MAX_CREDIT;
	unsigned int credit_fair =0;
	int credit_xtra =0;
	unsigned int min_credit_calc=0, max_credit_calc=0;

	counter++;	

	temp_vif=next_vif = NULL;

	if(list_empty(&active_vif_list) || total ==0)	
		goto out;
	if(CA->credit_balance > 0)
		credit_total += CA->credit_balance;
	
	printk("MINKOO%d: credit total %u total:%u credit balance:%u\n",counter,credit_total, total, CA->credit_balance);

	list_for_each_entry_safe(temp_vif, next_vif, &active_vif_list, vif_list){
		if(!temp_vif)	goto out;
		printk("MINKOO: unused credit:%u\n",temp_vif->remaining_credit);
		weight_left -= temp_vif->weight;
		credit_fair = ((credit_total * temp_vif->weight) + (total -1 )) / total;
		temp_vif->remaining_credit += credit_fair;
		
		//min-max credit input is a percentage
		if(temp_vif->min_credit!=0) min_credit_calc = (MAX_CREDIT/100) * temp_vif->min_credit;//
		if(temp_vif->max_credit!=0) max_credit_calc = (MAX_CREDIT/100) * temp_vif->max_credit;//
		
		if(temp_vif->min_credit!=0 || temp_vif->max_credit!=0){
			if(temp_vif->min_credit!=0 && temp_vif->remaining_credit < min_credit_calc){
				credit_total-=(min_credit_calc - temp_vif->remaining_credit);
				temp_vif->remaining_credit = min_credit_calc;
				list_del(&temp_vif->vif_list);
				list_add(&temp_vif->vif_list, &active_vif_list);
			}
			else if(temp_vif->max_credit!=0 && temp_vif->remaining_credit > max_credit_calc){
				credit_total+= (temp_vif->remaining_credit - max_credit_calc);
				temp_vif->remaining_credit = max_credit_calc;
				list_del(&temp_vif->vif_list);
				list_add(&temp_vif->vif_list, &active_vif_list);
			}
			goto skip;
		}
		
		credit_left += (temp_vif->remaining_credit - credit_fair);
		temp_vif->remaining_credit = credit_fair;  

/*
		if(temp_vif->remaining_credit <= MAX_CREDIT){
			credit_xtra = 1;
		}
		else
		{
//			credit_left += (temp_vif->remaining_credit - MAX_CREDIT);
			credit_left += (temp_vif->remaining_credit - credit_fair);		
			if(weight_left != 0){
				credit_total += ((credit_left*total)+(weight_left - 1))/weight_left;
				credit_left=0;
		}

			if(credit_xtra){
				list_del(&temp_vif->vif_list);
				list_add(&temp_vif->vif_list, &active_vif_list);
			}


//			temp_vif->remaining_credit = credit_fair;			
			if(CA->num_vif > 1)
				temp_vif->remaining_credit = credit_fair;
			else
				temp_vif->remaining_credit = MAX_CREDIT;

		}
*/		
skip:	
		if(temp_vif->need_reschedule == true)
			temp_vif->need_reschedule = false;
		printk("MINKOO: vif_id:%d, weight:%u, min:%d, max:%u, credit:%u, credit_fair:%u\n", temp_vif->id, temp_vif->weight, temp_vif->min_credit, temp_vif->max_credit, temp_vif->remaining_credit, credit_fair);
	}
	CA->credit_balance = credit_left;
	credit_left=0;
out:
	mod_timer(&CA->account_timer, jiffies + msecs_to_jiffies(10));
	return;
}

static ssize_t vif_write(struct file *file, const char __user *user_buffer, size_t count, loff_t *ppos)
{
        const char* filename = file->f_path.dentry->d_name.name;
	struct ancs_container *vif;
	static int cnt =1;
	if(cnt>1000) return 0;
	char *temp_buf;
	temp_buf = kmalloc(count,GFP_KERNEL | __GFP_NOWARN | __GFP_REPEAT);
	if(copy_from_user(temp_buf, user_buffer, count)){
		printk("copy fail\n");
		return 1;
	}
	char* input = strsep(&temp_buf,"\n");
	char *endptr;
	unsigned int value = simple_strtol(input, &endptr, 10);
	
        if(endptr == input && *endptr != '\0')
        {
               printk("invalid input!\n");
                return count;
        }

	vif = PDE_DATA(file_inode(file));
        printk("MINKOO_w: cnt:%d, filename:%s, input=%u, vif=%p\n", cnt++, filename, value, vif);	
	if(!(vif))
	{
		printk(KERN_INFO "NULL Data\n");
		return 0;
	}

	if(!strcmp(filename, "min_credit"))
        {
		vif->min_credit = value;
		printk("MINKOO: min_credit:%d\n", vif->min_credit);
		goto proc_out_w;
        }
	
	if(!strcmp(filename, "max_credit"))
        {
		vif->max_credit = value;
		printk("MINKOO: min_credit:%d\n", vif->max_credit);
		goto proc_out_w;
        }

	if(!strcmp(filename, "weight"))
        {
		CA->total_weight += value - vif->weight;
		vif->weight = value;
		printk("MINKOO: weight:%d", vif->weight);
		goto proc_out_w;
        }
	printk("match failure\n");
	return count;

proc_out_w:
        if(fileread == 0){
                fileread = 1;
		return count;
        }
        else
        {
                fileread = 0;
		return 0;
        }
}

static ssize_t vif_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	const char* filename = file->f_path.dentry->d_name.name;
	struct ancs_container *vif;	
	unsigned int len;
	char * temp_buf;
	temp_buf = kmalloc(sizeof(unsigned int),GFP_KERNEL | __GFP_NOWARN | __GFP_REPEAT);		
	
	vif = PDE_DATA(file_inode(file));
	if(!(vif)){
                printk(KERN_INFO "NULL Data\n");
                return 0;
        }
	printk("MINKOO_r: buf=%p, vif=%p\n", buf, vif);
//#if 0
	if(!strcmp(filename, "min_credit")){
		len = sprintf(temp_buf, "%u\n", vif->min_credit);
		if(copy_to_user(buf, temp_buf,strlen(temp_buf)))
			printk("MINKOO: copy fail\n");
		goto proc_out;
	}
	else if(!strcmp(filename, "max_credit")){
		len = sprintf(temp_buf, "%u\n", vif->max_credit);
                if(copy_to_user(buf, temp_buf,strlen(temp_buf)))
                        printk("MINKOO: copy fail\n"); 
		goto proc_out;
	}
	else if(!strcmp(filename, "weight")){
		len = sprintf(temp_buf, "%u\n", vif->weight);
                if(copy_to_user(buf, temp_buf,strlen(temp_buf)))
                        printk("MINKOO: copy fail\n"); 
		goto proc_out;
	}
	else if(!strcmp(filename, "remaining_credit")){
		len = sprintf(temp_buf, "%u\n", vif->remaining_credit);
                if(copy_to_user(buf, temp_buf,strlen(temp_buf)))
                        printk("MINKOO: copy fail\n"); 	
		goto proc_out;
	}
	else if(!strcmp(filename, "used_credit")){
		len = sprintf(temp_buf, "%u\n", vif->used_credit);
                if(copy_to_user(buf, temp_buf,strlen(temp_buf)))
                        printk("MINKOO: copy fail\n"); 
		goto proc_out;
	}
	else{
		count = sprintf(buf, "%s", "ERROR");
		return count;
	}
//#endif
proc_out:

	if(fileread == 0){
                fileread = 1;
                return len;
        }
        else
        {
                fileread = 0;
                return 0;
        }
}


static const struct file_operations vif_opt ={
	.write = vif_write,
	.read = vif_read,
};

int pay_credit(struct ancs_container *vif, unsigned int packet_data_size){
	//if date_len is zero then it means no fragment
	//printk(KERN_INFO "MINKOO:vif%u remaining credit:%u paying:%u",vif->id,vif->remaining_credit, packet_data_size);
	if(vif->remaining_credit == 0){
		//printk(KERN_INFO "PAYMENT SUCCESS\n");
		return PAY_SUCCESS;
	}
	if(vif->remaining_credit < packet_data_size){
		//printk(KERN_INFO "PAYMENT FAILURE\n");
		return PAY_FAIL;
	}
	else{
		vif->remaining_credit -= packet_data_size;
		//printk(KERN_INFO "PAYMENT SUCCESS\n");
		return PAY_SUCCESS;
	}
	return PAY_SUCCESS;
}

void new_vif(struct net_bridge_port *p){
	int idx;
	
	if(p==NULL){
		printk(KERN_ERR "MINKOO: new port pointer null err\n");
		return;
	}
	
	//initialize new ancs_container
	struct ancs_container *vif;
	vif = kmalloc(sizeof(struct ancs_container), GFP_KERNEL | __GFP_NOWARN | __GFP_REPEAT);
	INIT_LIST_HEAD(&vif->vif_list);
	vif->need_reschedule = false;
	vif->weight = vif_cnt;	//0 is arbitary value, give weight to check QOS algorithm working.
	vif->min_credit = 0;		//arbitary
	vif->max_credit = 0;		//arbitary
	vif->remaining_credit = 0; 
	vif->used_credit = 0;	
	vif->id = vif_cnt++;
	
	vif->p =p;
	p->vif=vif;

	//add vif to credit allocator list
	list_add(&vif->vif_list, &active_vif_list);
	
	//update function for credit allocator
	CA->total_weight += vif->weight;
        CA->num_vif++;
	
	//need to implement: proc_fs new
	idx = vif->id;
	proc_vif[idx].id = (int)vif->id;
	sprintf(proc_vif[idx].name, "vif%d", (int)vif->id);
	proc_vif[idx].dir = proc_mkdir(proc_vif[idx].name, proc_root_dir);
	proc_vif[idx].file[0] = proc_create_data("min_credit",0600, proc_vif[idx].dir, &vif_opt, vif);
	proc_vif[idx].file[1] = proc_create_data("max_credit",0600, proc_vif[idx].dir, &vif_opt, vif);
	proc_vif[idx].file[2] = proc_create_data("weight",0600, proc_vif[idx].dir, &vif_opt, vif);
	proc_vif[idx].file[3] = proc_create_data("remaining_credit",0600, proc_vif[idx].dir, &vif_opt, vif);
	proc_vif[idx].file[4] = proc_create_data("used_credit",0600, proc_vif[idx].dir, &vif_opt, vif);

	printk(KERN_INFO "MINKOO: new vif%d weight=%d, min=%d, max=%d\n", vif->id, vif->weight, vif->min_credit, vif->max_credit);
}

void del_vif(struct net_bridge_port *p){
	int idx;

	if(p == NULL){
		printk(KERN_ERR "MINKOO: del port pointer null\n");
		return;
	}
	else if(p->vif == NULL){
		printk(KERN_ERR "MINKOO: del vif pointer null\n");
                return;
	}
	else printk(KERN_INFO "MINKOO: delete vif%d\n", p->vif->id);
	
	//delete list from credit allocator
	list_del(&p->vif->vif_list);
	
	//update function for credit allocator
	CA->total_weight -= p->vif->weight;
        CA->num_vif--;
	
	
	//need to implerment: proc_fs del	
	idx = p->vif->id;
	remove_proc_entry("min_credit", proc_vif[idx].dir);
	remove_proc_entry("max_credit", proc_vif[idx].dir);
	remove_proc_entry("weight", proc_vif[idx].dir);
	remove_proc_entry("remaining_credit", proc_vif[idx].dir);
	remove_proc_entry("used_credit", proc_vif[idx].dir);
	remove_proc_entry(proc_vif[idx].name, proc_root_dir);
	
	//free memory		
	kfree(p->vif);
}

static int __init vif_init(void)
{
	int cpu = smp_processor_id();
	//struct ancs_container *vif, *next_vif;
	vif_cnt = 1;
	counter = 0;	

	//function pointer linking
	fp_pay = &pay_credit;
	fp_newvif = &new_vif;
	fp_delvif = &del_vif;	

	//credit allocator initialization
	CA = kmalloc(sizeof(struct credit_allocator), GFP_KERNEL | __GFP_NOWARN | __GFP_REPEAT);
	if (!CA)
		return -ENOMEM;	
	CA->total_weight = 0;
	CA->credit_balance = 0;
	CA->num_vif =0;
	//INIT_LIST_HEAD(&CA->active_vif_list);
	//spin_lock_init(&CA->active_vif_list_lock);

	//make proc directory
	proc_root_dir = proc_mkdir("oslab", NULL);
	
	//need to implement: traverse off_list and add to CA list
	//list_for_each_entry_safe(vif, next_vif, &off_list, off_list){
	//	new_vif(vif);
	//	printk("MINKOO: vif from off_list\n");
	//	list_del(&vif->off_list);
	//}	
	
	//setting up timer for callback function
	setup_timer(&CA->account_timer, credit_accounting, cpu );
	mod_timer(&CA->account_timer, jiffies + msecs_to_jiffies(10));

	printk(KERN_INFO "MINKOO: credit allocator init!!\n");	

	return 0;
}

static void __exit vif_exit(void)
{
	//struct list_head *p, *temp;
	struct ancs_container *vif, *next_vif;

	printk(KERN_INFO "MINKOO: credit allocator exit!!\n");

	//need to implement : traverse CA list and add to off_list
	list_for_each_entry_safe(vif, next_vif, &active_vif_list, vif_list){
	//	vif = list_entry(p, struct ancs_container, vif_list);
		printk("MINKOO: delvif%d\n", vif->id);
		del_vif(vif->p);
	//	INIT_LIST_HEAD(&vif->off_list);
	//	list_add(&vif->off_list, &off_list);
	}

	remove_proc_entry("oslab", NULL);

	//delete timer
	del_timer(&CA->account_timer);

	 //free CA
        kfree(CA);
	
	return;
}

module_init(vif_init);
module_exit(vif_exit);

MODULE_AUTHOR("Korea University");
MODULE_DESCRIPTION("OSLAB");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("ver 1.0");
