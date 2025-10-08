#include <efi.h>
#include <efilib.h>
#include "../include/types.h"

void KernelMain(Framebuffer *fb);

EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
Framebuffer framebuffer;

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, EFI_TEXT_ATTR(EFI_LIGHTCYAN, EFI_BLACK));
    
    Print(L"\n  MyOS UEFI Bootloader v1.0\n\r");
    Print(L"  ============================\n\r\n\r");
    
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS status = uefi_call_wrapper(BS->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);
    
    if(EFI_ERROR(status)) {
        Print(L"[ERROR] Failed to locate GOP\n\r");
        uefi_call_wrapper(BS->Stall, 1, 5000000);
        return status;
    }
    
    Print(L"[OK] Graphics Protocol located\n\r");
    
    status = uefi_call_wrapper(gop->SetMode, 2, gop, 0);
    
    framebuffer.base = (uint32_t*)gop->Mode->FrameBufferBase;
    framebuffer.width = gop->Mode->Info->HorizontalResolution;
    framebuffer.height = gop->Mode->Info->VerticalResolution;
    framebuffer.pixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;
    
    Print(L"[OK] Graphics: %dx%d\n\r", framebuffer.width, framebuffer.height);
    
    uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
    Print(L"[OK] Starting kernel...\n\r");
    uefi_call_wrapper(BS->Stall, 1, 2000000);
    
    KernelMain(&framebuffer);
    
    while(1) { }
    return EFI_SUCCESS;
}

#include "../kernel/kernel.c"
#include "../apps/notes.c"
