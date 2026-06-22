#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

// Wii Music Game IDs for the three main regions (PAL, NTSC-U, NTSC-J)
const char* REGIONS[] = {
    "/title/00010000/52363450", // R64P (PAL)
    "/title/00010000/52363445", // R64E (USA)
    "/title/00010000/5236344a" // R64J (JAP)
};
const char* REGION_NAMES[] = { "PAL (Europe)", "NTSC (USA)", "NTSC-J (Japan)" };

void InitialiseVideo() {
    VIDEO_Init();
    WPAD_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
}

int CopyFile(const char *srcPath, const char *destPath) {
    FILE *src = fopen(srcPath, "rb");
    if (!src) return -1;

    FILE *dest = fopen(destPath, "wb");
    if (!dest) {
        fclose(src);
        return -2;
    }

    char buffer;
    size_t bytesRead;
    while ((bytesRead = fread(&buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(&buffer, 1, bytesRead, dest);
    }

    fclose(src);
    fclose(dest);
    return 0;
}

int CheckWiiMusicRegion() {
    static char nameList[ISFS_MAXPATH] __attribute__((aligned(32)));
    u32 numEntries = 0;
    
    for (int i = 0; i < 3; i++) {
        s32 ret = ISFS_ReadDir(REGIONS[i], nameList, &numEntries);
        if (ret >= 0) return i; // Returns the index of the found region
    }
    return -1; // Game save not found on NAND
}

int main(int argc, char **argv) {
    InitialiseVideo();
   
    printf("\n ======================================= ");
    printf("\n WII EXTEND MUSIC MANAGER v1.0.0 ");
    printf("\n ======================================= \n\n");

    s32 isfs_status = ISFS_Initialize();
    if (isfs_status != 0) {
        printf("[ERROR] NAND initialization failed! Code: %d\n", isfs_status);
        while(1);
    }

    int usb_retry = 0;
    bool usb_mounted = false;
    while (usb_retry < 5 && !usb_mounted) {
        if (fatInitDefault()) { usb_mounted = true; } 
        else { usb_retry++; sleep(1); }
    }

    if (!usb_mounted) {
        printf("[ERROR] Failed to mount USB storage! Please insert a FAT32 drive.\n");
        while(1);
    }

    int regIdx = CheckWiiMusicRegion();
    if (regIdx == -1) {
        printf("[ERROR] Wii Music save data not found on real NAND!\n");
        printf("Please launch the game at least once to create a save file.\n");
    } else {
        printf("[SUCCESS] Detected Wii Music region: %s\n\n", REGION_NAMES[regIdx]);
        mkdir("usb:/WiiExtendMusic", 0777);
    }

    int current_slot = 1;
    char nandPath[ISFS_MAXPATH];
    char usbPath[ISFS_MAXPATH];
    snprintf(nandPath, sizeof(nandPath), "%s/data/data.bin", REGIONS[regIdx]);

    printf("REMOTE CONTROLS:\n");
    printf(" -> D-PAD RIGHT / LEFT: Change USB Slot\n");
    printf(" -> BUTTON A: Export 100 videos from NAND to USB (Backup)\n");
    printf(" -> BUTTON B: Import videos from USB to NAND (Restore)\n");
    printf(" -> HOME BUTTON: Exit\n\n");

    while(1) {
        printf("\r[SELECTED SLOT: %d] ", current_slot);
        fflush(stdout);

        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);

        if (pressed & WPAD_BUTTON_RIGHT) {
            if (current_slot < 10) current_slot++;
        }
        if (pressed & WPAD_BUTTON_LEFT) {
            if (current_slot > 1) current_slot--;
        }

        if (pressed & WPAD_BUTTON_A && regIdx != -1) {
            printf("\n[PROCESSING] Exporting data...");
            snprintf(usbPath, sizeof(usbPath), "usb:/WiiExtendMusic/wiimusic_slot_%d.bin", current_slot);
            int res = CopyFile(nandPath, usbPath);
            if (res == 0) printf("\n[OK] Videos successfully saved to USB Slot %d!\n", current_slot);
            else printf("\n[ERROR] Save file not found or write-protected. Code: %d\n", res);
        }

        if (pressed & WPAD_BUTTON_B && regIdx != -1) {
            printf("\n[PROCESSING] Importing data...");
            snprintf(usbPath, sizeof(usbPath), "usb:/WiiExtendMusic/wiimusic_slot_%d.bin", current_slot);
            int res = CopyFile(usbPath, nandPath);
            if (res == 0) printf("\n[OK] Slot %d successfully loaded to your Wii!\n", current_slot);
            else printf("\n[ERROR] No backup file found in USB Slot %d.\n", current_slot);
        }

        if (pressed & WPAD_BUTTON_HOME) {
            break;
        }
        VIDEO_WaitVSync();
    }

    ISFS_Deinitialize();
    exit(0);
    return 0;
}
