#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xf8cdd757, "module_layout" },
	{ 0x9c122cd1, "ib_set_device_ops" },
	{ 0xf4b9b193, "kmalloc_caches" },
	{ 0x27d8667f, "execute_in_process_context" },
	{ 0xd2b09ce5, "__kmalloc" },
	{ 0x4b7f0aff, "crypto_alloc_shash" },
	{ 0x349cba85, "strchr" },
	{ 0xbe0ff601, "ib_device_put" },
	{ 0x7af4a299, "dev_get_flags" },
	{ 0xfca337e2, "remap_vmalloc_range" },
	{ 0xe25ee9d3, "_raw_write_lock_irqsave" },
	{ 0xad46531f, "ib_get_eth_speed" },
	{ 0xcb75e0c2, "rdma_get_gid_attr" },
	{ 0x659744b8, "ip_local_out" },
	{ 0xb1e2ba7a, "vlan_dev_vlan_id" },
	{ 0x15244c9d, "boot_cpu_data" },
	{ 0xffae8e8b, "nsecs_to_jiffies" },
	{ 0xa2005ede, "sock_release" },
	{ 0xcc084f7, "dst_release" },
	{ 0xb3635b01, "_raw_spin_lock_bh" },
	{ 0xf1b501ca, "skb_clone" },
	{ 0xb6fc7fb2, "dev_get_by_name" },
	{ 0x2124474, "ip_send_check" },
	{ 0xd2da1048, "register_netdevice_notifier" },
	{ 0xd567e68e, "rdma_link_register" },
	{ 0x9b7fe4d4, "__dynamic_pr_debug" },
	{ 0xccd4c999, "__sg_page_iter_start" },
	{ 0x4cc8f9c8, "init_timer_key" },
	{ 0xf6106730, "sock_create_kern" },
	{ 0xa6093a32, "mutex_unlock" },
	{ 0x999e8297, "vfree" },
	{ 0xb348a850, "ex_handler_refcount" },
	{ 0xdd64e639, "strscpy" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0x689a0125, "rdma_link_unregister" },
	{ 0x8542e30, "skb_scrub_packet" },
	{ 0x15ba50a6, "jiffies" },
	{ 0x9d0d6206, "unregister_netdevice_notifier" },
	{ 0x1c1b9f8e, "_raw_write_unlock_irqrestore" },
	{ 0xd6dbebdc, "udp_sock_create6" },
	{ 0x5ab63eb1, "setup_udp_tunnel_sock" },
	{ 0x89407a46, "ib_dealloc_device" },
	{ 0x4977b3c, "dev_mc_add" },
	{ 0xa23695dc, "ib_unregister_device_queued" },
	{ 0xd6025ada, "__pskb_pull_tail" },
	{ 0xb44ad4b3, "_copy_to_user" },
	{ 0x17de3d5, "nr_cpu_ids" },
	{ 0xe1c0f56a, "mark_driver_unsupported" },
	{ 0x6a3f6948, "udp_tunnel_sock_release" },
	{ 0xec02a35f, "del_timer_sync" },
	{ 0xfb578fc5, "memset" },
	{ 0x2a70864d, "__cpu_possible_mask" },
	{ 0x3812050a, "_raw_spin_unlock_irqrestore" },
	{ 0x9a76f11f, "__mutex_init" },
	{ 0x27e1a049, "printk" },
	{ 0xe1537255, "__list_del_entry_valid" },
	{ 0x449ad0a7, "memcmp" },
	{ 0xc2a3da6e, "crypto_shash_update" },
	{ 0x479c3c86, "find_next_zero_bit" },
	{ 0xfaef0ed, "__tasklet_schedule" },
	{ 0xa1c76e0a, "_cond_resched" },
	{ 0x4d9b652b, "rb_erase" },
	{ 0xa7b9bcfb, "dev_mc_del" },
	{ 0x9fac42e8, "ib_query_port" },
	{ 0x94ca0b57, "skb_push" },
	{ 0x41aed6e7, "mutex_lock" },
	{ 0xe78bfacd, "crc32_le" },
	{ 0x7c9ca58f, "__sg_page_iter_next" },
	{ 0xcd56dc62, "kernel_sock_shutdown" },
	{ 0x5e31111c, "ib_umem_get" },
	{ 0x44d6c673, "ib_device_set_netdev" },
	{ 0xf1969a8e, "__usecs_to_jiffies" },
	{ 0x3bd9699f, "ib_unregister_device_and_put" },
	{ 0x77bbb598, "rdma_put_gid_attr" },
	{ 0x9545af6d, "tasklet_init" },
	{ 0x28985b9f, "mod_timer" },
	{ 0x7b56b034, "ib_unregister_driver" },
	{ 0x98bd7f6, "skb_pull" },
	{ 0x36f927cb, "ipv6_stub" },
	{ 0xa29abf62, "init_net" },
	{ 0x68f31cbd, "__list_add_valid" },
	{ 0xaf924fe7, "vlan_dev_real_dev" },
	{ 0xef4dc8d7, "ib_dispatch_event" },
	{ 0xf11543ff, "find_first_zero_bit" },
	{ 0x82072614, "tasklet_kill" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0xe75cd266, "ib_device_get_by_netdev" },
	{ 0x4f13dc57, "skb_queue_tail" },
	{ 0x1b43c203, "_dev_info" },
	{ 0x736b5662, "_raw_read_lock_irqsave" },
	{ 0xb601be4c, "__x86_indirect_thunk_rdx" },
	{ 0x3c5dfeb0, "__alloc_skb" },
	{ 0x49c41a57, "_raw_spin_unlock_bh" },
	{ 0x5635a60a, "vmalloc_user" },
	{ 0xdb7305a1, "__stack_chk_fail" },
	{ 0x88cf2433, "ib_register_device" },
	{ 0x1d24c881, "___ratelimit" },
	{ 0x67b4bbab, "kfree_skb" },
	{ 0x2ea2c95c, "__x86_indirect_thunk_rax" },
	{ 0x49b45dc2, "rdma_read_gid_attr_ndev_rcu" },
	{ 0xa16c8613, "_raw_read_unlock_irqrestore" },
	{ 0xe156f99a, "crypto_destroy_tfm" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xf86c8d03, "kmem_cache_alloc_trace" },
	{ 0x51760917, "_raw_spin_lock_irqsave" },
	{ 0x30c5ac1b, "_ib_alloc_device" },
	{ 0xa5526619, "rb_insert_color" },
	{ 0x74286f62, "ip_route_output_flow" },
	{ 0x37a0cba, "kfree" },
	{ 0xf7d6dcb4, "ib_sg_to_pages" },
	{ 0x69acdf38, "memcpy" },
	{ 0x2d8cd911, "ib_modify_qp_is_ok" },
	{ 0x4ca9669f, "scnprintf" },
	{ 0x63c4d61f, "__bitmap_weight" },
	{ 0xdea8077e, "skb_dequeue" },
	{ 0xc4655669, "udp_sock_create4" },
	{ 0x28318305, "snprintf" },
	{ 0x7a19d39f, "ip6_local_out" },
	{ 0x517acd8d, "rdma_find_gid_by_port" },
	{ 0x5bce0a2a, "skb_put" },
	{ 0x362ef408, "_copy_from_user" },
	{ 0x382ebd48, "ib_device_get_by_name" },
	{ 0x8dbb4ea2, "ib_umem_release" },
	{ 0x158d2a71, "__ip_select_ident" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=ib_core,ip6_udp_tunnel,udp_tunnel,ib_uverbs";


MODULE_INFO(srcversion, "A02D808303862401FF534D4");
MODULE_INFO(rhelversion, "8.5");
