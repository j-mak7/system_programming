#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>

#define REG_MODES_COUNT     0
#define REG_MODES_ADDR      8
#define REG_BUFFER_ADDR     16
#define REG_STATUS          24
#define REG_MODES_BUFFER    32
#define REG_FRAMEBUFFER     4096

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct
{
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} __attribute__((__packed__)) BITMAPFILEHEADER;

typedef struct
{
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} __attribute__((__packed__)) BITMAPINFOHEADER;

#define BMP_OFFSET (sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))

int create_bmp_file(FILE *file, int width, int height) {
    int row_size = width * 3;
    int padding = (4 - (row_size % 4)) % 4;
    int row_padded = row_size + padding;

    int image_size = row_padded * height;
    int file_size = BMP_OFFSET + image_size;

    BITMAPFILEHEADER file_header = {
            .bfType = 0x4D42,
            .bfSize = file_size,
            .bfReserved1 = 0,
            .bfReserved2 = 0,
            .bfOffBits = BMP_OFFSET
    };

    BITMAPINFOHEADER info_header = {
            .biSize = 40,
            .biWidth = width,
            .biHeight = height,
            .biPlanes = 1,
            .biBitCount = 24,
            .biCompression = 0,
            .biSizeImage = image_size,
            .biXPelsPerMeter = 0,
            .biYPelsPerMeter = 0,
            .biClrUsed = 0,
            .biClrImportant = 0
    };

    if (fwrite(&file_header, sizeof(file_header), 1, file) != 1) return -1;
    if (fwrite(&info_header, sizeof(info_header), 1, file) != 1) return -1;

//    for (int y = 0; y < height; y++) {
//        for (int x = 0; x < width; x++) {
//            // BGR формат
//            fputc(0, file); // Blue
//            fputc(0, file);   // Green
//            fputc(0, file);   // Red
//        }
//        for (int p = 0; p < padding; p++) {
//            fputc(0, file);
//        }
//    }

    printf("Created BMP: %dx%d\n", width, height);
    printf("Row size: %d, padding: %d\n", row_size, padding);
    printf("File size: %d bytes\n", file_size);
    return 0;
}

volatile sig_atomic_t stop;

void inthand(int signum)
{
    stop = 1;
}

int main() {
    signal(SIGINT, inthand);
    FILE *bin = fopen("/home/julia/qemu/build/bar2.bin", "r+b");

    setbuf(bin, NULL);
    int status = 1;
    fseek(bin, REG_STATUS, SEEK_SET);
    status = 1;
    fwrite(&status, sizeof(int), 1, bin);
    //fflush(bin);
    fseek(bin, REG_STATUS, SEEK_SET);
    fread(&status, sizeof(int), 1, bin);
    printf("STATUS: %d\n", status);
    while (!stop) {
        usleep(10000);
        fseek(bin, REG_STATUS, SEEK_SET);
        fread(&status, sizeof(int), 1, bin);
        //printf("STATUS: %d\n", status);
        if (status == 2) {
            FILE *framebuffer = fopen("/home/julia/qemu/build/framebuffer.bmp", "w+b");
            fseek(bin, REG_STATUS, SEEK_SET);
            int zero = 0;
            fwrite(&zero, sizeof(int), 1, bin);
            //fflush(bin);
            fseek(bin, 0, SEEK_SET);
            int count_mode = 0;
            fread(&count_mode, sizeof(int), 1, bin);
            printf("%d\n", count_mode);
            int current_mode = 0;
            fseek(bin, REG_BUFFER_ADDR, SEEK_SET);
            fread(&current_mode, sizeof(int), 1, bin);
            int height = 0;
            int width = 0;
            fseek(bin, REG_MODES_BUFFER + current_mode * 8, SEEK_SET);
            fread(&height, sizeof(int), 1, bin);
            fread(&width, sizeof(int), 1, bin);
            int padding = (4 - (width * 3 % 4)) % 4;
            printf("padding: %d\n", padding);
            int buf_size = (width * 3 + padding) * height;
            printf("BUF SIZE: %d\n", buf_size);
            fseek(bin, REG_FRAMEBUFFER, SEEK_SET);
            create_bmp_file(framebuffer, width, height);
            char *buffer = malloc(buf_size);
            size_t bytes_read = fread(buffer, 1, buf_size, bin);
            printf("Bytes read: %ld\n", bytes_read);
            fseek(framebuffer, BMP_OFFSET, SEEK_SET);
            fwrite(buffer, 1, bytes_read, framebuffer);
//            char* ptr = buffer;
//            int copy_hight=height;
//            int count = 1;
//            while (copy_hight != 0) {
//                fwrite(ptr, 1, width * 3, framebuffer);
//                fseek(framebuffer, BMP_OFFSET + ((width * 3)) * count, SEEK_SET);
//                copy_hight--;
//                count++;
//                if (copy_hight != 0) {
//                buffer += (width * 3 + padding);}
//            }
            //fflush(bin);
            free(buffer);
            fseek(bin, REG_STATUS, SEEK_SET);
            int done_status = 1;
            fwrite(&done_status, sizeof(int), 1, bin);
            //fflush(bin);
            fclose(framebuffer);
        }
        usleep(1000);
    }
    fclose(bin);
}

//int main() {
//	//FILE* bmp = fopen("data.bmp", "rb");
//	FILE* bin = fopen("/home/julia/qemu/build/bar2.bin", "w");
//    uint32_t count_mode = 3;
//    struct_mode s = {800, 600};
//    uint64_t addr_struct_mode = (uint64_t)&s;
//    struct_mode buf_mode[] =  {
//            {640, 480},
//            {800, 600},
//            {1024, 768}
//    };
//    uint64_t addr_buf_mode = (uint64_t)buf_mode;
//    uint8_t status = 1; //0-error, 1-ok
//    uint8_t* framebuffer;
//	if (!bin) {
//		printf("ERROR\n");
//		return 1;
//	}
//    fwrite(&count_mode, 4, 1, bin);
//    fwrite(&addr_struct_mode, 8, 1, bin);
//    fwrite(&addr_buf_mode, 8, 1, bin);
//    fwrite(&status, 1, 1, bin);
//    fwrite(&framebuffer, sizeof(framebuffer), 1, bin);
//    fwrite(&buf_mode, sizeof(buf_mode), 1, bin);
//    printf("count_mode: %d, addr_struct_mode: %llu, addr_buf_mode: %llu, "
//           "status: %d\n", count_mode, addr_struct_mode, addr_buf_mode, status);
//
//	fclose(bin);
//}
