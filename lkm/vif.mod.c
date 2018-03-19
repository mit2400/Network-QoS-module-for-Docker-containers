#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xb587fcbd, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x5c2e3421, __VMLINUX_SYMBOL_STR(del_timer) },
	{ 0x5ee52022, __VMLINUX_SYMBOL_STR(init_timer_key) },
	{ 0xeacae873, __VMLINUX_SYMBOL_STR(fp_delvif) },
	{ 0x8e196f42, __VMLINUX_SYMBOL_STR(fp_newvif) },
	{ 0x7a2af7b4, __VMLINUX_SYMBOL_STR(cpu_number) },
	{ 0xd197fc3f, __VMLINUX_SYMBOL_STR(fp_pay) },
	{ 0xdb7305a1, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0xb742fd7, __VMLINUX_SYMBOL_STR(simple_strtol) },
	{ 0x85df9b6c, __VMLINUX_SYMBOL_STR(strsep) },
	{ 0x362ef408, __VMLINUX_SYMBOL_STR(_copy_from_user) },
	{ 0xd2b09ce5, __VMLINUX_SYMBOL_STR(__kmalloc) },
	{ 0xa38caae0, __VMLINUX_SYMBOL_STR(mod_timer) },
	{ 0x15ba50a6, __VMLINUX_SYMBOL_STR(jiffies) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x6721577c, __VMLINUX_SYMBOL_STR(remove_proc_entry) },
	{ 0xb44ad4b3, __VMLINUX_SYMBOL_STR(_copy_to_user) },
	{ 0x88db9f48, __VMLINUX_SYMBOL_STR(__check_object_size) },
	{ 0x8e8908c4, __VMLINUX_SYMBOL_STR(PDE_DATA) },
	{ 0xe6e4df53, __VMLINUX_SYMBOL_STR(proc_create_data) },
	{ 0x6fe150b7, __VMLINUX_SYMBOL_STR(proc_mkdir) },
	{ 0x91715312, __VMLINUX_SYMBOL_STR(sprintf) },
	{ 0x8d081e, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0x7033fedd, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=bridge";


MODULE_INFO(srcversion, "814324ED3643D572DAF584A");
