#include <gba.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include "Save.h"
#include "EepromSave.h"
#include "FlashSave.h"
#include "WhiteScreenPatch.h"
#include "my_io_scsd.h"
// u32 gbaRomSize = 66;

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
#define BUF_LEN 0x10000
EWRAM_BSS u8 copyBuf[BUF_LEN];
//---------------------------------------------------------------------------------
// Program entry point
//---------------------------------------------------------------------------------

#define AGB_ROM ((u32 *)0x8000000)
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
	const DISC_INTERFACE *sc_inter = &_my_io_scsd ;
	_my_io_scsd.startup();
	_my_io_scsd.shutdown();
	printf("Fat32 test\n\n");
	sc_inter->startup();
	if (fatMountSimple("fat",sc_inter)) {
	// if(fatInit (2, true)){
		printf("FAT system initialised\n");
	} else {
		printf("FAT system failed!\n");
		waitForever();
	}


	char rom_name[] = "fat:/run.gba";
	FILE *gba_rom = fopen(rom_name,"rb");
	if(gba_rom){
		printf("Opened:%s\n",rom_name);
	}else{
		printf("GBA open failed:%s\n",rom_name);
		waitForever();
	}
    fseek(gba_rom, 0, SEEK_END);
    int gbaRomSize = ftell(gba_rom);
	romSize = gbaRomSize;
    fseek(gba_rom, 0, SEEK_SET);

    printf("The size of the file is:\n %u bytes\n", gbaRomSize);
	

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
	
    printf("File copied successfully.\n Now Patching\n");

	patchGeneralWhiteScreen();
	patchSpecificGame();
	
	printf("White Screen patch done!\nNow patching Save\n");
	
	///开始给存档打补丁
	
	const save_type_t* saveType = savingAllowed ? save_findTag() : NULL;
	if (saveType != NULL && saveType->patchFunc != NULL){
		bool done = saveType->patchFunc(saveType);
		if(!done)
			printf("Save Type Patch Error\n");
	}else{
		printf("No need to patch\n");
	}
	
	_SC_changeMode(SC_MODE_RAM_RO);
	printf("Press A to Start Game\n");
	pressToContinue(KEY_A);
	__asm("swi 0"); // Soft reset
    printf("panic!!.\n");
	waitForever();

	return 0;
}
