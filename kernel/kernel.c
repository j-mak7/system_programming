#include <stdint.h>
#include <stddef.h>
#include <efi.h>
#include <efilib.h>

#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_TAG_TYPE_MMAP 6
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER 8
//#define MULTIBOOT_TAG_TYPE_EFI_BS 18
#define MULTIBOOT_TAG_TYPE_EFI64 12


struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_mmap_entry
{
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t zero;
};

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot_mmap_entry entries[];
};


struct multiboot_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t reserved;
};

struct multiboot_tag_efi64
{
    uint32_t type;
    uint32_t size;
    uint64_t pointer;
};


void memset32(uint32_t *dest, uint32_t color, size_t count) {
    for (size_t i = 0; i < count; i++) {
        dest[i] = color;
    }
}

void draw_char(uint32_t *fb, uint32_t width, uint32_t height,
               uint32_t x, uint32_t y, char c, uint32_t color) {
    static const uint8_t font_basic[][8] = {
            { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},   // U+0030 (0)
            { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},   // U+0031 (1)
            { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},   // U+0032 (2)
            { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},   // U+0033 (3)
            { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},   // U+0034 (4)
            { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},   // U+0035 (5)
            { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},   // U+0036 (6)
            { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},   // U+0037 (7)
            { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},   // U+0038 (8)
            { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},   // U+0039 (9)
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0020 (space)
            { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+003A (:)
            { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},   // U+0041 (A)
            { 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},   // U+0042 (B)
            { 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},   // U+0043 (C)
            { 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},   // U+0044 (D)
            { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},   // U+0045 (E)
            { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},   // U+0046 (F)
            { 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},   // U+0047 (G)
            { 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},   // U+0048 (H)
            { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0049 (I)
            { 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},   // U+004A (J)
            { 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},   // U+004B (K)
            { 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},   // U+004C (L)
            { 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},   // U+004D (M)
            { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},   // U+004E (N)
            { 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},   // U+004F (O)
            { 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},   // U+0050 (P)
            { 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},   // U+0051 (Q)
            { 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},   // U+0052 (R)
            { 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},   // U+0053 (S)
            { 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0054 (T)
            { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},   // U+0055 (U)
            { 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0056 (V)
            { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},   // U+0057 (W)
            { 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},   // U+0058 (X)
            { 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},   // U+0059 (Y)
            { 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},   // U+005A (Z)
            { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}    // U+002D (-)
    };

    int index = 37;

    if (c >= '0' && c <= '9') index = c - '0';
    else if (c == ' ') index = 10;
    else if (c == ':') index = 11;
    else if (c == '-') index = 38;
    else if (c >= 'A' && c <= 'Z') index = 12 + (c - 'A');
    else if (c >= 'a' && c <= 'z') index = 12 + (c - 'a');
    else index = 37;

    for (int dy = 0; dy < 8; dy++) {
        for (int dx = 0; dx < 8; dx++) {
            if (font_basic[index][dy] & (1 << dx)) {
                uint32_t px = x + dx;
                uint32_t py = y + dy;
                if (px < width && py < height) {
                    fb[py * width + px] = color;
                }
            }
        }
    }
}

void draw_string(uint32_t *fb, uint32_t width, uint32_t height,
                 uint32_t x, uint32_t y, const char *str, uint32_t color) {
    uint32_t cx = x;

    for (int i = 0; str[i] != '\0'; i++) {

        if (str[i] == '\n') {
            y += 10;
            cx = x;
        }
        else {
            draw_char(fb, width, height, cx, y, str[i], color);
            cx += 9;
        }
    }
}

const char* get_type(uint32_t type) {
    switch (type) {
        case EfiReservedMemoryType:      return "Reserved";
        case EfiLoaderCode:              return "Usable";
        case EfiLoaderData:              return "Usable";
        case EfiBootServicesCode:        return "Usable";
        case EfiBootServicesData:        return "Usable";
        case EfiRuntimeServicesCode:     return "Type";
        case EfiRuntimeServicesData:     return "Reserved";
        case EfiConventionalMemory:      return "Usable";
        case EfiUnusableMemory:          return "Unusable";
        case EfiACPIReclaimMemory:       return "ACPI Data";
        case EfiACPIMemoryNVS:           return "ACPI NVS";
        case EfiMemoryMappedIO:          return "Reserved";
        case EfiMemoryMappedIOPortSpace: return "Reserved";
        case EfiPalCode:                 return "Reserved";
        case EfiMaxMemoryType:           return "Reserved";
        default:                         return "Unknown";
    }
}


int get_number_memory(uint32_t type) {
    switch (type) {
        case EfiReservedMemoryType:      return 0;
        case EfiLoaderCode:              return 1;
        case EfiLoaderData:              return 1;
        case EfiBootServicesCode:        return 1;
        case EfiBootServicesData:        return 1;
        case EfiRuntimeServicesCode:     return 2;
        case EfiRuntimeServicesData:     return 0;
        case EfiConventionalMemory:      return 1;
        case EfiUnusableMemory:          return 3;
        case EfiACPIReclaimMemory:       return 4;
        case EfiACPIMemoryNVS:           return 5;
        case EfiMemoryMappedIO:          return 0;
        case EfiMemoryMappedIOPortSpace: return 0;
        case EfiPalCode:                 return 0;
        case EfiMaxMemoryType:           return 0;
        default:                         return 6;
    }
}

void draw_number(uint32_t *fb, uint32_t width, uint32_t height, int x, int y, uint64_t val, uint32_t color) {
    draw_char(fb, width, height, x, y, '0', color);
    draw_char(fb, width, height, x+8, y, 'x', color);
    x += 16;
    char arr_val[16];
    for (int i = 0; i < 16; i++) {
        int n = val%16;
        if (n < 10) {arr_val[i] = n + '0';}
        else {arr_val[i] = 'A' + n - 10;}
        val = val / 16;
    }
    for (int i = 15; i >= 0; i--) {
        draw_char(fb, width, height, x, y, arr_val[i], color);
        x += 8;
    }
}

EFI_STATUS get_memory_map(EFI_BOOT_SERVICES* bs, uint32_t *fb, uint32_t width, uint32_t height) {
    EFI_STATUS status;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_version = 0;
    EFI_MEMORY_DESCRIPTOR* memory_map = NULL;
    //wrap_GetMemoryMap GetMemoryMap = (wrap_GetMemoryMap)bs->GetMemoryMap;
    status = bs->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_version);
    map_size += desc_size * 2;
    //wrap_AllocatePool AllocatePool = (wrap_AllocatePool)bs->AllocatePool;
    status = bs->AllocatePool(EfiLoaderData, map_size, (VOID **)&memory_map);
    if (status != EFI_SUCCESS) {return status;}
    status = bs->GetMemoryMap(&map_size, memory_map, &map_key, &desc_size, &desc_version);
    if (status != EFI_SUCCESS) {return status;}
    UINTN count = map_size / desc_size;
    int y = 70;
    //draw_string(fb, width, height, 10, 50, "Linux-like Memory Map:", 0xFFFFFF);
    int i = 0;
    //for (int i = 0; i < count; i++) {
    while (i < count) {
        EFI_MEMORY_DESCRIPTOR* d1 = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)memory_map + (i * desc_size));
        EFI_MEMORY_DESCRIPTOR* d2 = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)memory_map + ((i + 1) * desc_size));
        if (y < height - 20) {
            draw_string(fb, width, height, 10, y, get_type(d1->Type), 0xFFFFFF);
            draw_number(fb, width, height, 100, y, d1->PhysicalStart, 0xFFFFFF);
        }
        uint64_t end = d1->PhysicalStart + d1->NumberOfPages * 4096;
        i++;

        while (get_number_memory(d1->Type) == get_number_memory(d2->Type) && end == d2->PhysicalStart) {
            i += 1;
            if (i <= count) {
                end = d2->PhysicalStart + d2->NumberOfPages * 4096;
                d2 = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)memory_map + (i * desc_size));
            }
            //draw_string(fb, width, height, 40 + i * 10, y, get_type(d1->Type), 0xFFFFFF);
        }
        draw_char(fb, width, height, 250, y, '-', 0xFFFFFF);
        draw_number(fb, width, height, 260, y, end - 1, 0xFFFFFF);
        y += 10;

    }
    //wrap_FreePool FreePool = (wrap_FreePool)bs->FreePool;
    status = bs->FreePool((VOID *)memory_map);
    return status;
}

void kernel_main(uint32_t magic, uintptr_t mb_info_addr) {
    if (magic != 0x36D76289) {
        return;
    }

    struct multiboot_tag *tag = (struct multiboot_tag*)(mb_info_addr + 8);
    struct multiboot_tag_framebuffer *fb_tag = NULL;
    struct multiboot_tag_mmap *mmap_tag = NULL;
    struct multiboot_tag_efi64 *bs_tag = NULL;

    uint32_t tag_size;
    int array[25];
    int i = 0;
    while (tag->type != 0) {
        array[i] = tag->type;
        i++;
        switch (tag->type) {
            case MULTIBOOT_TAG_TYPE_FRAMEBUFFER:
                fb_tag = (struct multiboot_tag_framebuffer*)tag;
                break;
            case MULTIBOOT_TAG_TYPE_EFI64:
                bs_tag = (struct multiboot_tag_efi64*)tag;
                break;
//            case MULTIBOOT_TAG_TYPE_MMAP:
//                mmap_tag = (struct multiboot_tag_mmap*)tag;
//                break;
        }
        tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7));
    }

    if (!fb_tag) {
        return;
    }

    uint32_t *framebuffer = (uint32_t*)(uintptr_t)fb_tag->framebuffer_addr;
    uint32_t width = fb_tag->framebuffer_width;
    uint32_t height = fb_tag->framebuffer_height;

    memset32(framebuffer, 0x0000FF, width * height);
    if (bs_tag) {
        EFI_SYSTEM_TABLE* systab = (EFI_SYSTEM_TABLE*)(uintptr_t)bs_tag->pointer;
        if (!systab) {
            draw_string(framebuffer, width, height, 10, 50, "No systab", 0xFF0000);
            return;
        }
        EFI_BOOT_SERVICES* bs = systab->BootServices;
        if (!bs) {
            draw_string(framebuffer, width, height, 10, 50, "No BootServices", 0xFF0000);
            return;
        }
        int size = 0;
        EFI_STATUS status = get_memory_map(bs, framebuffer, width, height);
        //print_memory_info(framebuffer, width, height, mmap_tag, 0xFFFFFF);
    }
    else {draw_char(framebuffer, width, height, 10, 30, 'N', 0xFF0000);}

    while (1);
}