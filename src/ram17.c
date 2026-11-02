
/**
 * This tool is the refactored result of the UEFI research.
 * However, it only for a specific laptop model and BIOS version.
 * For details on porting to different configurations, see README.md.
 * Most memory addresses are reverse-engineered magic numbers.
 */

#include "./gnu-efi/inc/efi.h"
#include "./gnu-efi/inc/efilib.h"
#include "gnu-efi/inc/efiapi.h"
#include "gnu-efi/inc/efierr.h"

// RVA stands for "Relative Virtual Address"
#define PAGE_INDEX_RVA 0x1AE790U
#define SEND_FORM_RVA  0x27298U
#define MODE_RVA       0x1AB000U

#define NUM_UNLOCK_PAGES 6
static const UINT16 UNLOCK_PAGES_IDS[NUM_UNLOCK_PAGES] = {
    0x2711, 0x2712, 0x2713, 0x2714, 0x2715, 0x2716
};
static const char *UNLOCK_PAGES_NAMES[NUM_UNLOCK_PAGES] = {
    "MAIN", "ADVANCED", "CHIPSET", "SECURITY", "BOOT", "EXIT"
};

/**
 * FormBrowser2 protocol interface.
 *
 * gnu-efi does not ship this protocol header; the real declaration is in
 * EDK2 at MdePkg/Include/Protocol/FormBrowser2.h. Only the first member is
 * used here: SendForm points into the live AMITSE/SetupBrowser driver and
 * serves as the anchor for recovering that driver's image base. Callback is
 * unused.
 *
 * x86_64 layout:
 *   +0x00  SendForm  function pointer
 *   +0x08  Callback  function pointer
 *
 * The parameter types are simplified because this code never calls SendForm;
 * it only reads the function pointer's value.
 */
typedef struct {
    EFI_STATUS (EFIAPI *SendForm)(
        const void *, void *, UINTN, const EFI_GUID *, UINT16, const void *, UINTN *
    );
    void *Callback;
} EFI_FORM_BROWSER2_PROTOCOL;

/**
 * EFI_FORM_BROWSER2_PROTOCOL_GUID:
 *   b9d4c360-bcfb-4f9b-9298-53c136982258
 *
 * Standard UEFI protocol GUID. It identifies the live setup browser driver;
 * the RVAs used against that driver are the BIOS-build-specific part.
 *
 * Not const because gnu-efi's LibLocateProtocol() takes a non-const EFI_GUID *.
 */
static EFI_GUID gEfiFormBrowser2ProtocolGuid = {
    0xb9d4c360, 0xbcfb, 0x4f9b,
    {0x92, 0x98, 0x53, 0xc1, 0x36, 0x98, 0x22, 0x58}
};

/**
 * Load base of the executing AMITSE/FormBrowser2 driver.
 *
 * Derived at runtime in get_table_base() as:
 *
 *     ImageBase = fb->SendForm - SEND_FORM_RVA
 *
 * It anchors every fixed-RVA lookup against the live driver, in particular the
 * page index pointer at PAGE_INDEX_RVA and the one-shot mode byte at MODE_RVA.
 *
 * NULL until get_table_base() succeeds. The address is specific to this BIOS
 * build and changes between boots, so it must never be hardcoded. It refers to
 * RAM only; nothing here is written back to flash.
 */
static UINT8 *ImageBase;

/**
 * Read 2 bytes and combine them into a 16-bit unsigned integer.
 * Parsing in little-endian (LSB first).
 */
static UINT16 u16(const void *mem)
{
    UINT16 i16 = 0;
    const UINT8 *p = mem;

    i16 |= p[0];
    i16 |= p[1] << 8;

    return i16;
}

/**
 * Read 4 bytes and combine them into a 32-bit unsigned integer.
 * Parsing in little-endian (LSB first).
 */
static UINT32 u32(const void *mem)
{
    UINT32 i32 = 0;
    const UINT8 *p = mem;

    i32 |= p[0];
    i32 |= p[1] << 8;
    i32 |= p[2] << 16;
    i32 |= (UINT32) p[3] << 24;

    return i32;
}

/**
 * Read 8 bytes and combine them into a 64-bit unsigned integer.
 * Parsing in little-endian (LSB first).
 */
static UINT64 u64(const void *mem)
{
    UINT64 i64 = 0;
    const UINT8 *p = mem;

    i64 |= (UINT64) u32(&p[0]);
    i64 |= (UINT64) u32(&p[4]) << 32;

    return i64;
}

/**
 * Locate the live AMITSE/FormBrowser2 driver and return its page index table.
 *
 * The FormBrowser2 protocol object is only used as an anchor: SendForm is a
 * known function at RVA SEND_FORM_RVA in this BIOS build, so subtracting that
 * RVA from its runtime address yields the driver's load base. The page index
 * pointer is then read from the driver image at PAGE_INDEX_RVA.
 *
 * Side effect: stores the recovered driver image base in ImageBase.
 *
 * Returns NULL if the protocol is unavailable or SendForm is null; otherwise
 * returns the runtime address of the page index table.
 */
static UINT8 *get_table_base(void)
{
    EFI_STATUS r;
    EFI_FORM_BROWSER2_PROTOCOL *fb;

    r = LibLocateProtocol(&gEfiFormBrowser2ProtocolGuid, (void **)&fb);
    if (EFI_ERROR(r) || !fb || !fb->SendForm)
        return 0;

    ImageBase = (UINT8 *)(UINTN)fb->SendForm - SEND_FORM_RVA;
    return (UINT8 *)(UINTN)u64(ImageBase + PAGE_INDEX_RVA);
}

/**
 * For a given form ID, return form address and form flags.
 * If found multiple occurrences of the form ID, return the first match.
 *
 * table_base points to a structure like this:
 * table_base
 *   +0x00        UINT32 count
 *   +0x04        UINT32 offset[0]   ---- page record 0
 *   +0x08        UINT32 offset[1]   ---- page record 1
 *   +0x0C        UINT32 offset[2]   ---- page record 2
 *   ...
 *
 * page record
 *   +0x0A   UINT16   form ID
 *   +0x14   UINT32   flags
 */
static BOOLEAN find_page(UINT16 form_id, UINT8 **form_addr, UINT32 *form_flags)
{
    UINT8 *p;
    UINT32 i;
    UINT32 form_count;
    UINT32 offset;
    UINT8 *table_base;

    table_base = get_table_base();
    if (!table_base)
        return FALSE;

    form_count = u32(table_base);

    for (i = 0; i < form_count; i++)
    {
        offset = u32(table_base + 4 + 4 * i);
        p = table_base + offset;
        if (u16(p + 0x0a) == form_id)
        {
            *form_addr = p;
            *form_flags = u32(p + 0x14);
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * Clear the exclusion flag for each requested AMI top-level page and set the
 * AMITSE one-shot mode byte.
 *
 * For each form ID in UNLOCK_PAGES_IDS, find_page() locates the live page
 * record. Bit 0 of its flags field (+0x14) is the "excluded/hidden" bit, so
 * clearing it makes the page visible. Missing pages are reported and skipped.
 *
 * The mode byte at MODE_RVA must be set for the flag changes to take effect;
 * it is written only if at least one page was patched. All changes are RAM
 * only and disappear on reboot.
 */
void uefi_patch(void)
{
    BOOLEAN patched = FALSE;
    UINT8 *form_addr;
    UINT32 form_flags;

    for (UINTN i = 0; i < NUM_UNLOCK_PAGES; i++)
    {
        if (!find_page(UNLOCK_PAGES_IDS[i], &form_addr, &form_flags))
        {
            Print(L"Could not find form \"%a\", skipping...\n", UNLOCK_PAGES_NAMES[i]);
            continue;
        }

        form_flags &= ~1U;
        *(UINT32 *)(form_addr + 0x14) = form_flags;
        patched = TRUE;
    }

    // AMITSE one-shot mode byte: required for the page flags to take effect.
    if (patched && ImageBase)
        *(UINT8 *)(ImageBase + MODE_RVA) = 1;
}

/**
 * EFI application entry point.
 *
 * Validates the system table, initializes gnu-efi, applies the RAM patch, and
 * prints a short status message. The user should then return to ASUS Setup so
 * the browser can rebuild its menus.
 *
 * Returns EFI_NOT_FOUND if the required console or boot services are missing,
 * otherwise EFI_SUCCESS. No rollback is performed; a reboot clears the patch.
 */
EFI_STATUS efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    if (
        !SystemTable ||
        !SystemTable->BootServices ||
        !SystemTable->ConIn ||
        !SystemTable->ConOut
    ) {
        return EFI_NOT_FOUND;
    }

    InitializeLib(ImageHandle, SystemTable);
    uefi_patch();
    Print(L"Patch active. Return to ASUS Setup.\n");
    return EFI_SUCCESS;
}
