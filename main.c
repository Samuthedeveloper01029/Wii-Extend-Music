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

// Real NAND Wii Music Paths
const char* REGIONS[] = {
    "/title/00010000/52363450/data/data.bin", // PAL
    "/title/00010000/52363445/data/data.bin", // USA
    "/title/00010000/5236344a/data/data.bin" // JAP
};
const char* REGION_NAMES[] = { "PAL (Europe)", "NTSC (USA)", "NTSC-J (Japan)" };

#define SAVE_BUFFER_SIZE (1024 * 1024) 
char *saveBuffer = NULL;
long saveFileSize = 0;

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

int ReadNandToBuffer(const char* nandPath) {
    ISFS_Initialize();
    s32 fd = ISFS_Open(nandPath, ISFS_OPEN_READ);
    if (fd < 0) {
        ISFS_Deinitialize();
        return -1; 
    }
    if (!saveBuffer) saveBuffer = malloc(SAVE_BUFFER_SIZE);
    saveFileSize = ISFS_Read(fd, saveBuffer, SAVE_BUFFER_SIZE);
    ISFS_Close(fd);
    ISFS_Deinitialize(); 
    return (saveFileSize > 0) ? 0 : -2;
}

int WriteBufferToUsb(const char* usbPath) {
    if (!fatInitDefault()) return -3;
    mkdir("usb:/WiiExtendMusic", 0777);
    FILE *dest = fopen(usbPath, "wb");
    if (!dest) {
        fatUnmount("usb:/");
        return -4; 
    }
    fwrite(saveBuffer, 1, saveFileSize, dest);
    fclose(dest);
    fatUnmount("usb:/"); 
    return 0;
}

int ReadUsbToBuffer(const char* usbPath) {
    if (!fatInitDefault()) return -3;
    FILE *src = fopen(usbPath, "rb");
    if (!src) {
        fatUnmount("usb:/");
        return -5;
    }
    if (!saveBuffer) saveBuffer = malloc(SAVE_BUFFER_SIZE);
    fseek(src, 0, SEEK_END);
    saveFileSize = ftell(src);
    fseek(src, 0, SEEK_SET);
    fread(saveBuffer, 1, saveFileSize, src);
    fclose(src);
    fatUnmount("usb:/");
    return 0;
}

int WriteBufferToNand(const char* nandPath) {
    ISFS_Initialize();
    s32 fd = ISFS_Open(nandPath, ISFS_OPEN_WRITE);
    if (fd < 0) {
        ISFS_Deinitialize();
        return -1;
    }
    s32 ret = ISFS_Write(fd, saveBuffer, saveFileSize);
    ISFS_Close(fd);
    ISFS_Deinitialize();
    return (ret >= 0) ? 0 : -6;
}

int main(int argc, char **argv) {
    // Forza la rimozione delle protezioni hardware se AHBPROT è attivo nel loader
    HaltDeviceType(NAND_DEVICE_TYPE); 
    
    InitialiseVideo();
   
    printf("\n ======================================= ");
    printf("\n WII EXTEND MUSIC MANAGER v1.0.1 ");
    printf("\n ======================================= \n\n");

    int regIdx = -1;
    ISFS_Initialize();
    for (int i = 0; i < 3; i++) {
        s32 fd = ISFS_Open(REGIONS[i], ISFS_OPEN_READ);
        if (fd >= 0) {
            ISFS_Close(fd);
            regIdx = i;
            break;
        }
    }
    ISFS_Deinitialize();

    if (regIdx == -1) {
        printf("[ERROR] Wii Music save data NOT found on Real NAND!\n");
        printf("Please run the game normally once to create a save.\n");
    } else {
        printf("[SUCCESS] Detected Wii Music region: %s\n\n", REGION_NAMES[regIdx]);
    }

    int current_slot = 1;
    char usbFilePath[256]; // Fixato errore di allocazione array stringa

    printf("REMOTE CONTROLS:\n");
    printf(" -> D-PAD RIGHT / LEFT: Change USB Slot\n");
    printf(" -> BUTTON A: Export 100 videos from Wii to USB (Backup)\n");
    printf(" -> BUTTON B: Import videos from USB to Wii (Restore)\n");
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
            printf("\n[BACKUP] Reading from Real NAND...");
            int readRes = ReadNandToBuffer(REGIONS[regIdx]);
            if (readRes == 0) {
                printf(" [OK]\n[BACKUP] Writing to USB Storage...");
                snprintf(usbFilePath, sizeof(usbFilePath), "usb:/WiiExtendMusic/wiimusic_slot_%d.bin", current_slot);
                int writeRes = WriteBufferToUsb(usbFilePath);
                if (writeRes == 0) printf(" [SUCCESS!]\n-> Saved to USB Slot %d.\n", current_slot);
                else printf(" [FAILED] USB Error: %d\n", writeRes);
            } else {
                printf(" [FAILED] NAND Error: %d\n", readRes);
            }
        }

        if (pressed & WPAD_BUTTON_B && regIdx != -1) {
            printf("\n[RESTORE] Reading from USB...");
            snprintf(usbFilePath, sizeof(usbFilePath), "usb:/WiiExtendMusic/wiimusic_slot_%d.bin", current_slot);
            int readRes = ReadUsbToBuffer(usbFilePath);
            if (readRes == 0) {
                printf(" [OK]\n[RESTORE] Writing to Real NAND...");
                int writeRes = WriteBufferToNand(REGIONS[regIdx]);
                if (writeRes == 0) printf(" [SUCCESS!]\n-> Slot %d loaded into Wii.\n", current_slot);
                else printf(" [FAILED] NAND Write Error: %d\n", writeRes);
            } else {
                printf(" [FAILED] USB Read Error: %d\n", readRes);
            }
        }

        if (pressed & WPAD_BUTTON_HOME) break;
        VIDEO_WaitVSync();
    }

    if (saveBuffer) free(saveBuffer);
    exit(0);
    return 0;
}
