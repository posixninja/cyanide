/**
  * GreenPois0n Cynanide - kernel.c
  * Copyright (C) 2010 Chronic-Dev Team
  * Copyright (C) 2010 Joshua Hill
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lock.h"
#include "image.h"
#include "patch.h"
#include "nvram.h"
#include "kernel.h"
#include "common.h"
#include "commands.h"
#include "functions.h"
#include "filesystem.h"

static char* gKernelAddr = NULL;
char* gBootArgs = NULL;
char** gKernelPhyMem = SELF_KERNEL_PHYMEM;

int(*kernel_atv_load)(char* boot_path, char** output) = NULL;
int(*kernel_load)(void* input, int max_size, char** output) = NULL;

static void kernel_usage(void);

void* find_kernel_bootargs() {
	return find_string(TARGET_BASEADDR, TARGET_BASEADDR, 0x40000, "rd=md0");
}

void* find_kernel_load() {
	return find_function("kernel_load", TARGET_BASEADDR, TARGET_BASEADDR);
}

void* find_kernel_phymem() {
	return 0;
}

int kernel_init() {
	gBootArgs = find_kernel_bootargs();
	if(gBootArgs == NULL) {
		puts("Unable to find gBootArgs\n");
	} else {
		printf("Found gBootArgs at 0x%x\n", gBootArgs);
	}
	kernel_load = find_kernel_load();
	if(kernel_load == NULL) {
		puts("Unable to find kernel_load function\n");
	} else {
		printf("Found kernel_load at 0x%x\n", kernel_load);
	}
	cmd_add("kernel", &kernel_cmd, "operations for filesystem kernel");
	return 0;
}

int kernel_cmd(int argc, CmdArg* argv) {
	char* action = NULL;
	unsigned int size = 0;
	unsigned char* address = NULL;
	if(argc < 2) {
		kernel_usage();
		return 0;
	}

	action = argv[1].string;
	if(!strcmp(action, "load")) {
		if(argc != 4) {
			kernel_usage();
			return -1;
		}
		address = (unsigned char*) argv[2].uinteger;
		size = argv[3].uinteger;
		if(address == NULL || size == 0) {
			puts("Invalid source address or size for kernel load\n");
			return -1;
		}

		if(strstr((char*) (IBOOT_BASEADDR + 0x200), "k66ap")) {
			if(kernel_atv_load == NULL) {
				puts("kernel_atv_load not available for AppleTV\n");
				return -1;
			}
			printf("Loading AppleTV kernelcache from %s\n", KERNEL_PATH);
			if(kernel_atv_load(KERNEL_PATH, &gKernelAddr) != 0) {
				puts("Failed to load AppleTV kernelcache\n");
				return -1;
			}
		} else {
			if(kernel_load == NULL) {
				puts("kernel_load not available\n");
				return -1;
			}
			printf("Loading kernelcache from 0x%x\n", address);
			if(kernel_load((void*) address, size, &gKernelAddr) != 0) {
				puts("Failed to load kernelcache\n");
				return -1;
			}
		}
		void* phy_mem = (gKernelPhyMem != NULL) ? *gKernelPhyMem : NULL;
		printf("kernelcache prepped at %p with phymem %p\n", gKernelAddr, phy_mem);
	}
	else if(!strcmp(action, "patch")) {
		if(argc != 2 && argc != 4) {
			kernel_usage();
			return -1;
		}

		if(argc == 4) {
			address = (unsigned char*) argv[2].uinteger;
			size = argv[3].uinteger;
			if(address == NULL || size == 0) {
				puts("Invalid address or size for kernel patch\n");
				return -1;
			}
			printf("patching kernel at %p (size 0x%x)...\n", address, size);
			patch_kernel(address, size);
		} else {
			if(gKernelAddr == NULL) {
				puts("No kernelcache loaded to patch\n");
				return -1;
			}
			printf("patching kernel at loaded address %p...\n", gKernelAddr);
			patch_kernel(gKernelAddr, 0xC00000);
		}
	}
	else if(!strcmp(action, "bootargs")) {
		if(argc < 3) {
			puts("usage: kernel bootargs <string>\n");
			return -1;
		}
		kernel_bootargs(argc, argv);
	}
	else if(!strcmp(action, "boot")) {
		if(gKernelAddr == NULL) {
			puts("Load and patch a kernel before booting\n");
			return -1;
		}
		if(gKernelPhyMem == NULL || *gKernelPhyMem == NULL) {
			puts("Kernel physical memory pointer not set\n");
			return -1;
		}
		if(jump_to == NULL) {
			puts("jump_to function not available\n");
			return -1;
		}
		printf("booting kernel...\n");
		jump_to(3, gKernelAddr, *gKernelPhyMem);
	}
	else {
		kernel_usage();
	}
	return 0;
}

static void kernel_usage(void) {
	puts("usage: kernel <load/patch/bootargs/boot> [options]\n");
	puts("  load <address> <size>         \tload filesystem kernel to address\n");
	puts("  patch [address] [size]        \tpatch kernel at address in memory\n");
	puts("                                 \t(defaults to loaded kernelcache)\n");
	puts("  bootargs <string>             \treplace current bootargs with another\n");
	puts("  boot                          \tboot a loaded kernel\n");
}

int kernel_bootargs(int argc, CmdArg* argv) {
	if(gBootArgs == NULL) {
		puts("Unable to set kernel bootargs");
		return -1;
	}

	int i = 0;
	int size = strlen(gBootArgs);
	for(i = 2; i < argc; i++) {
		if(i == 2) {
			strncpy(gBootArgs, "", size);
		}
		if(i > 2) {
			strncat(gBootArgs, " ", size);
		}
		strncat(gBootArgs, argv[i].string, size);
	}

	return 0;
}

/*
int kernel_load(void* dest, int max_size, int* size) {
	unsigned int* file_size = 0;
	void* image = (void*) 0x43000000;
    ImageHeader* header = (ImageHeader*) image;

	puts("Loading kernelcache image\n");

    // Mount disk and load kernelcache
	fs_mount("nand0a", "hfs", "/boot");
	fs_load_file(KERNEL_PATH, image, file_size);

    // Decrypt image
    image_decrypt(image);

    // Find start of (actual) data
	unsigned char* data = image_find_tag(image, IMAGE_DATA, header->fullSize);
    data += sizeof(ImageTagHeader);

    // Assume data starts with a complzss header
    ImageComp* comp = (ImageComp*)data;
    FLIPENDIAN(comp->signature);
    FLIPENDIAN(comp->compressionType);
    FLIPENDIAN(comp->compressedSize);
    FLIPENDIAN(comp->uncompressedSize);
    
    if(comp->signature != IMAGE_COMP || comp->compressionType != IMAGE_LZSS) {
        puts("Didn't find expected COMPLZSS header\n");
        fs_unmount("/boot");
        return;
    }

    enter_critical_section();
    printf("LZSS compressed: %d uncompressed: %d\n", comp->compressedSize, comp->uncompressedSize);
    exit_critical_section();

    char *lzss_data = ((char *)(comp) + sizeof(ImageComp));
    int len = lzss_decompress(dest, comp->uncompressedSize, lzss_data, comp->compressedSize);

    enter_critical_section();
    printf("Actually uncompressed: %d\n", len);
	exit_critical_section();
	fs_unmount("/boot");

	puts("Finished loading kernelcache\n");
	*size = len;
	return 0;
}
*/

int kernel_patch(void* address) {
	printf("Not implemented yet\n");
	return 0;
}

