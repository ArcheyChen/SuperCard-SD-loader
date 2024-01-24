#include <gba.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include "Save.h"
#include "EepromSave.h"
#include "FlashSave.h"

u32 romSize;

bool savingAllowed = true;//有某个游戏是不能开启存档的，否则出错
// Values for changing mode
#define SC_MODE_RAM 0x5
#define SC_MODE_MEDIA 0x3 
#define SC_MODE_RAM_RO 0x1
// 1=ram(readonly), 5=ram, 3=SD interface?
inline void __attribute__((optimize("O0"))) _SC_changeMode(u16 mode) {
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

void pressToContinue(u16 key){
	u16 pressed = 0;
	do {
		VBlankIntrWait();
		scanKeys();
		pressed = keysDown();
	} while (!(pressed & key));
}
//---------------------------------------------------------------------------------
void waitForever() {
//---------------------------------------------------------------------------------
	while (1)
		VBlankIntrWait();
}
#define BUF_LEN 0x20000
EWRAM_DATA u8 copyBuf[BUF_LEN];
//---------------------------------------------------------------------------------
// Program entry point
//---------------------------------------------------------------------------------
u32 prefetchPatch[8] = {
	0xE59F000C,	// LDR  R0, =0x4000204
	0xE59F100C, // LDR  R1, =0x4000
	0xE4A01000, // STRT R1, [R0]
	0xE59F0008, // LDR  R0, =0x80000C0 (this changes, depending on the ROM)
	0xE1A0F000, // MOV  PC, R0
	0x04000204,
	0x00004000,
	0x080000C0
};
#define AGB_ROM ((vu32 *)0x8000000)
#define AGB_PRAM (volatile void *)0x5000000
#define AGB_VRAM (volatile void *)0x6000000
#define AGB_SRAM (volatile void *)0xE000000

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160
#define SCREEN_MARGIN_RIGHT 7

#define BIT_MASK(len)       ( (1<<(len))-1 )

int main(void) {
//---------------------------------------------------------------------------------


	// the vblank interrupt must be enabled for VBlankIntrWait() to work
	// since the default dispatcher handles the bios flags no vblank handler
	// is required
	irqInit();
	irqEnable(IRQ_VBLANK);

	consoleDemoInit();

	printf("Fat32 test\n\n");

	if (fatInitDefault()) {
		printf("FAT system initialised\n");
	} else {
		printf("FAT system failed!\n");
		waitForever();
	}


	char rom_name[] = "sram-zelda-origin.gba";
	FILE *gba_rom = fopen(rom_name,"rb");
	if(gba_rom){
		printf("Open OK:%s\n",rom_name);
	}else{
		printf("GBA open failed:%s\n",rom_name);
		waitForever();
	}
    fseek(gba_rom, 0, SEEK_END);
    romSize = ftell(gba_rom);
    fseek(gba_rom, 0, SEEK_SET);

    printf("The size of the file is:\n %d bytes\n", romSize);
	

    // 拷贝过程
    size_t bytesRead;
	printf("Copying:");
	int offset = 0;
    while ((bytesRead = fread(copyBuf, 1, BUF_LEN, gba_rom)) > 0) {
		_SC_changeMode(SC_MODE_RAM);
		#pragma unroll
		for(int i=0;i<BUF_LEN/4;i++){
			AGB_ROM[i + offset] = ((u32*)copyBuf)[i];
		}
		offset+=BUF_LEN/4;
		_SC_changeMode(SC_MODE_MEDIA);
		printf("#");
    }
	
    fclose(gba_rom);
	
	_SC_changeMode(SC_MODE_RAM);
	
    printf("File copied successfully.\n Now Patching");
	u32 entryPoint = *(u32*)0x08000000;
	entryPoint -= 0xEA000000;
	entryPoint += 2;
	prefetchPatch[7] = 0x08000000+(entryPoint*4);

	u32 patchOffset = 0x01FFFFDC;

	{
		vu32 *patchAddr = (vu32*)(0x08000000+patchOffset);
		for(int i=0;i<8;i++){
			patchAddr[i] = prefetchPatch[i];
		}
	}

	u32 branchCode = 0xEA000000+(patchOffset/sizeof(u32))-2;
	*(vu32*)0x08000000 = branchCode;

	u32 searchRange = 0x08000000+romSize;
	if (romSize > 0x01FFFFDC) searchRange = 0x09FFFFDC;

	// General fix for white screen crash
	// Patch out wait states
	for (u32 addr = 0x080000C0; addr < searchRange; addr+=4) {
		if (*(u32*)addr == 0x04000204 &&
		  (*(u8*)(addr-1) == 0x00 || *(u8*)(addr-1) == 0x03 || *(u8*)(addr-1) == 0x04 || *(u8*)(addr+7) == 0x04
		  || *(u8*)(addr-1) == 0x08 || *(u8*)(addr-1) == 0x09
		  || *(u8*)(addr-1) == 0x47 || *(u8*)(addr-1) == 0x81 || *(u8*)(addr-1) == 0x85
		  || *(u8*)(addr-1) == 0xE0 || *(u8*)(addr-1) == 0xE7 || *(u16*)(addr-2) == 0xFFFE)) 
		{
			*(vu32*)addr = 0;
		}
	}

	// Also check at 0x410
	if (*(u32*)0x08000410 == 0x04000204) {
		*(vu32*)0x08000410 = 0;
	}
	printf("White Screen patch done!\n");
	
	///开始给存档打补丁

	
	const struct save_type_t* saveType = savingAllowed ? save_findTag() : NULL;
	if (saveType != NULL && saveType->patchFunc != NULL){
		bool err = saveType->patchFunc(saveType);
		printf("Save Type Patch Error\n");
	}
	
	_SC_changeMode(SC_MODE_RAM_RO);
	printf("Press A to Start Game\n");
	pressToContinue(KEY_A);
	__asm("swi 0"); // Soft reset
    printf("panic!!.\n");
	waitForever();

	return 0;
}
