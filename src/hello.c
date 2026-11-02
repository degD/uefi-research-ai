/**
 * Traditional introduction program for EFI programming.
 * Compile using the Makefile and run it! For details,
 * see REFERENCES section of README.md.
 */

#include "./gnu-efi/inc/efi.h"
#include "./gnu-efi/inc/efilib.h"

EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
   InitializeLib(ImageHandle, SystemTable);
   Print(L"Hello, world!\n");

   return EFI_SUCCESS;
}
