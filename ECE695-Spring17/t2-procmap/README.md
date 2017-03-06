The new function in `task_mmu.c` is `show_vma_page_map()`.
Once line of `base.c` is changed to make /proc/PID/maps writable.

By default, the `show_vma_page_map()` function is not enabled,
hence the `cat /proc/maps` has the default output.

To enable this feature, just do:
	`echo y > maps`
To disable this feature, just do:
	`echo n > maps`
