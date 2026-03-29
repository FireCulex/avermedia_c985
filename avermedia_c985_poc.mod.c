#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};


MODULE_INFO(depends, "videodev,videobuf2-v4l2,videobuf2-common,videobuf2-vmalloc");

MODULE_ALIAS("pci:v00001AF2d0000A001sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "E23B5774E2A98DE954CB561");
