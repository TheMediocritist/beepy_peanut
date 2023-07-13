#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#define SCREEN_WIDTH 400
#define SCREEN_HEIGHT 240
#define SCREEN_BYTES_PER_PIXEL 2

typedef struct {
    unsigned short x;
    unsigned short y;
} Point;

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} Color;

typedef struct {
    int fd;
    unsigned short *buffer;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
} Framebuffer;

Framebuffer fb;

char **file_list;
int max_files = 10;
int num_files = 0;
int selected_file = 0;

void file_list_callback(const char *filename, void *userdata) {
    int len = strlen(filename);
    if (strcmp(filename + len - 3, ".gb") == 0 || strcmp(filename + len - 4, ".gbc") == 0) {
        if (num_files + 1 > max_files) {
            max_files += 10;
            file_list = realloc(file_list, max_files * sizeof(char*));
        }
        file_list[num_files] = realloc(NULL, (len + 1) * sizeof(char));
        strcpy(file_list[num_files], filename);
        num_files++;
    }
}

void rom_list_init() {
    //fb.fd = open("/dev/fb0", O_RDWR);
    //if (fb.fd == -1) {
    //    perror("Unable to open framebuffer");
    //    exit(1);
   // }

    //if (ioctl(fb.fd, FBIOGET_FSCREENINFO, &fb.finfo) == -1) {
    //    perror("Unable to retrieve framebuffer fixed info");
    //    exit(1);
    //}

    //if (ioctl(fb.fd, FBIOGET_VSCREENINFO, &fb.vinfo) == -1) {
    //    perror("Unable to retrieve framebuffer variable info");
    //    exit(1);
    //}

    //int screen_size = fb.vinfo.xres * fb.vinfo.yres * fb.vinfo.bits_per_pixel / 8;
    //fb.buffer = (unsigned short *)mmap(0, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fd, 0);
    //if (fb.buffer == MAP_FAILED) {
    //    perror("Unable to map framebuffer");
    //    exit(1);
    //}

    file_list = realloc(NULL, max_files * sizeof(char*));
    file_list[0] = strdup("ROM1.gb");
    file_list[1] = strdup("ROM2.gb");
    file_list[2] = strdup("ROM3.gb");
    num_files = 3;
}

void rom_list_cleanup() {
    for (int i = 0; i < num_files; i++) {
        free(file_list[i]);
    }
    free(file_list);

    munmap(fb.buffer, fb.vinfo.xres * fb.vinfo.yres * fb.vinfo.bits_per_pixel / 8);
    close(fb.fd);
}

void draw_text(const char *text, int x, int y, Color color) {
    int i, j;
    int len = strlen(text);
    unsigned short *ptr = fb.buffer + (y * fb.vinfo.xres + x);

    for (i = 0; i < 8; i++) {
        unsigned char mask = 0x80;
        for (j = 0; j < 8; j++) {
            if (*text & mask) {
                *(ptr + j) = (color.r << 11) | (color.g << 5) | color.b;
            } else {
                *(ptr + j) = 0;
            }
            mask >>= 1;
        }
        ptr += fb.vinfo.xres;
        text++;
    }
}

void redraw_menu_screen() {
    memset(fb.buffer, 0xFF, fb.vinfo.xres * fb.vinfo.yres * SCREEN_BYTES_PER_PIXEL);

    if (num_files == 0) {
        draw_text("Place ROMs in \"Data/com.timhei.peanutgb\" folder", 20, 110, (Color){0, 0, 0});
        draw_text("or \"Data/user.xxxx.peanutgb\" folder", 65, 135, (Color){0, 0, 0});
    }

    for (int i = 0; i < 8; i++) {
        if (i >= num_files)
            break;
        int selected_file_offset = selected_file - 7;
        if (selected_file_offset < 0)
            selected_file_offset = 0;

        if (i == selected_file) {
            for (int j = 0; j < 30; j++) {
                int y = i * 30 + j;
                unsigned short *ptr = fb.buffer + (y * fb.vinfo.xres);
                for (int k = 0; k < fb.vinfo.xres; k++) {
                    *(ptr + k) = (j < 7 || j > 22) ? 0 : 0xFFFF;
                }
            }
            draw_text(file_list[i], 20, (i * 30) + 7, (Color){0, 0, 0});
        } else if (selected_file > 7 && i == 7) {
            for (int j = 0; j < 30; j++) {
                int y = i * 30 + j;
                unsigned short *ptr = fb.buffer + (y * fb.vinfo.xres);
                for (int k = 0; k < fb.vinfo.xres; k++) {
                    *(ptr + k) = (j < 7 || j > 22) ? 0 : 0xFFFF;
                }
            }
            draw_text(file_list[i + selected_file_offset], 20, (i * 30) + 7, (Color){0, 0, 0});
        } else if (selected_file > 7) {
            draw_text(file_list[i + selected_file_offset], 20, (i * 30) + 7, (Color){0, 0, 0});
        } else {
            draw_text(file_list[i], 20, (i * 30) + 7, (Color){0, 0, 0});
        }
    }
}

char *rom_list_update() {
    // TODO: Implement button handling for navigation and selection
    return NULL;
}

//int main() {
//    rom_list_init();
//    redraw_menu_screen();
//    // TODO: Add main loop for input handling and screen updating using rom_list_update() and redraw_menu_screen()
//    rom_list_cleanup();
//    return 0;
//}
