/*---------------------------------------------------------------------------------

	Some emulators need the bios from your gba in order to use SWI calls
	
	This little piece of code will read the bios and save it to your
	SD/CF card device using the magic of libfat.
	
	Some cards are supported be default, the binary will need patched with
	the appropriate DLDI file for newer cards.

---------------------------------------------------------------------------------*/


#include <gba.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>


// Values for changing mode
#define SC_MODE_RAM 0x5
#define SC_MODE_MEDIA 0x3 
#define SC_MODE_RAM_RO 0x1
// 1=ram(readonly), 5=ram, 3=SD interface?
void _SC_changeMode(u16 mode) {
	vu16 *unlockAddress = (vu16*)0x09FFFFFE;
	*unlockAddress = 0xA55A ;
	*unlockAddress = 0xA55A ;
	*unlockAddress = mode ;
	*unlockAddress = mode ;
} 
//ROM:
//write able = SC_MODE_RAM
//close = SC_MODE_MEDIA

//SRAM:
//read: SC_MODE_RAM_RO
//close:SC_MODE_MEDIA

//---------------------------------------------------------------------------------
void waitForever() {
//---------------------------------------------------------------------------------
	while (1)
		VBlankIntrWait();
}
void tonccpy(void *dst, const void *src, uint size)
{
	if (size == 0 || dst == NULL || src == NULL)
		return;

	uint count;
	vu16 *dst16;	 // hword destination
	u8  *src8;	  // byte source

	// Ideal case: copy by 4x words. Leaves tail for later.
	if (((u32)src|(u32)dst) %4 == 0 && size >= 4) {
		u32 *src32= (u32*)src;
		vu32 *dst32= (vu32*)dst;

		count = size/4;
		uint tmp = count&3;
		count /= 4;

		// Duff's Device, good friend!
		switch(tmp) {
			do {	*dst32++ = *src32++;
		case 3:	 *dst32++ = *src32++;
		case 2:	 *dst32++ = *src32++;
		case 1:	 *dst32++ = *src32++;
		case 0:	 ; } while (count--);
		}

		// Check for tail
		size &= 3;
		if (size == 0)
			return;

		src8 = (u8*)src32;
		dst16 = (vu16*)dst32;
	} else {
		// Unaligned

		uint dstOfs = (u32)dst&1;
		src8 = (u8*)src;
		dst16 = (vu16*)(dst-dstOfs);

		// Head: 1 byte.
		if (dstOfs != 0) {
			*dst16 = (*dst16 & 0xFF) | *src8++<<8;
			dst16++;
			if (--size == 0)
				return;
		}
	}

	// Unaligned main: copy by 2x byte.
	count = size/2;
	while (count--) {
		*dst16++ = src8[0] | src8[1]<<8;
		src8 += 2;
	}

	// Tail: 1 byte.
	if (size & 1)
		*dst16 = (*dst16 &~ 0xFF) | *src8;
}
#define BUF_LEN 0x10000
EWRAM_DATA char copyBuf[BUF_LEN];//32KB
//---------------------------------------------------------------------------------
// Program entry point
//---------------------------------------------------------------------------------

#define AGB_ROM (volatile void *)0x8000000
#define AGB_PRAM (volatile void *)0x5000000
#define AGB_VRAM (volatile void *)0x6000000
#define AGB_SRAM (volatile void *)0xE000000

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160
#define SCREEN_MARGIN_RIGHT 7
int main(void) {
//---------------------------------------------------------------------------------


	// the vblank interrupt must be enabled for VBlankIntrWait() to work
	// since the default dispatcher handles the bios flags no vblank handler
	// is required
	irqInit();
	irqEnable(IRQ_VBLANK);

	consoleDemoInit();

	iprintf("Fat32 test\n\n");

	if (fatInitDefault()) {
		iprintf("FAT system initialised\n");
	} else {
		iprintf("FAT system failed!\n");
		waitForever();
	}

	// DIR *d;
    // struct dirent *dir;
    // d = opendir("."); // 打开当前目录

    // if (d) {
    //     while ((dir = readdir(d)) != NULL) {
    //         printf("%s\n", dir->d_name); // 打印目录项的名称
    //     }
    //     closedir(d);
	// 	iprintf("print dir completed\n");
    // } else {
    //     perror("opendir failed");
	// 	waitForever();
    // }

	char rom_name[] = "sram-zelda-origin.gba";
	FILE *gba_rom = fopen(rom_name,"rb");
	if(gba_rom){
		iprintf("Open OK:%s\n",rom_name);
	}else{
		iprintf("GBA open failed:%s\n",rom_name);
		waitForever();
	}
	// 移动到文件末尾
    fseek(gba_rom, 0, SEEK_END);
    // 获取文件大小
    int romSize = ftell(gba_rom);
    // 重置文件指针到文件开始
    fseek(gba_rom, 0, SEEK_SET);

    printf("The size of the file is:\n %d bytes\n", romSize);
	
	u32 ptr = 0x08000000;

	if (romSize > 0x2000000) {romSize = 0x2000000;iprintf("file bigger than 32MB,resize to 32MB\n");}
	iprintf("Loading:");
	for (u32 len = romSize; len > 0; len -= BUF_LEN) {
		if (fread(&copyBuf, 1, (len>BUF_LEN ? BUF_LEN : len), gba_rom) > 0) {
			_SC_changeMode(SC_MODE_RAM);
			// memcpy((void*)ptr,copyBuf,BUF_LEN);
			tonccpy((void*)ptr,copyBuf,BUF_LEN);
			_SC_changeMode(SC_MODE_MEDIA);
			ptr += BUF_LEN;
			iprintf("#");
		} else {
			break;
		}
	}
	
	iprintf("\nLoadCompleted\n");
	
	_SC_changeMode(SC_MODE_MEDIA);
	ptr = 0x08000000;
	iprintf("Cmping...\n");
    fseek(gba_rom, 0, SEEK_SET);//移动到开始
	for (u32 len = romSize; len > 0; len -= BUF_LEN) {
		if (fread(&copyBuf, 1, (len>BUF_LEN ? BUF_LEN : len), gba_rom) > 0) {
			_SC_changeMode(SC_MODE_RAM);
			if(memcmp(copyBuf,(void*)ptr,BUF_LEN)){
				iprintf("ROM ERR AT:%X\n",romSize - len);
				waitForever();
			}
			_SC_changeMode(SC_MODE_MEDIA);
			
			iprintf("#");
		} else {
			break;
		}
	}
	
	_SC_changeMode(SC_MODE_RAM_RO);

	iprintf("Boot the game\n");
	REG_IE = 1;
	__asm("swi 0"); // Soft reset
	
	iprintf("panic\n");
	waitForever();

	return 0;
}


