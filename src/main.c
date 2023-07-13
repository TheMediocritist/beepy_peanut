#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/mman.h>

#include "rom_list.h"

#if defined(ENABLE_SOUND_MINIGB)
#include "minigb_apu/minigb_apu.h"
#endif

#include "peanut_gb.h"

#define FRAMEBUFFER_DEVICE "/dev/fb0"
#define LCD_ROWSIZE 400


// fd = input device, ev = input event
int fd;
struct input_event ev;

int fbfd;
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
char *fbp = 0;
int screensize;

static uint8_t *FrameBuffer;
int is_rom_chosen = 0;
int startSelectDelay = 30;
//SoundSource *gameSoundSource;

struct priv_t
{
    /* Pointer to allocated memory holding GB file. */
    uint8_t *rom;
    /* Pointer to allocated memory holding save file. */
    uint8_t *cart_ram;

    uint8_t fb[LCD_HEIGHT][LCD_WIDTH];
};

struct gb_s gb;
struct priv_t priv =
    {
        .rom = NULL,
        .cart_ram = NULL
    };
const double target_speed_ms = (double)1000.0 / (double)VERTICAL_SYNC;
double speed_compensation = 0.0;
uint_fast32_t new_ticks, old_ticks;
enum gb_init_error_e gb_ret;
/* Must be freed */
char *rom_file_name = NULL;
char *save_file_name = NULL;

static int update(void* userdata);
void quit();
void out();

/**
 * Returns a byte from the ROM file at the given address.
 */
uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
	const struct priv_t * const p = gb->direct.priv;
	return p->rom[addr];
}

/**
 * Returns a byte from the cartridge RAM at the given address.
 */
uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
	const struct priv_t * const p = gb->direct.priv;
	return p->cart_ram[addr];
}

/**
 * Writes a given byte to the cartridge RAM at the given address.
 */
void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr,
		       const uint8_t val)
{
	const struct priv_t * const p = gb->direct.priv;
	p->cart_ram[addr] = val;
}

/**
 * Returns a pointer to the allocated space containing the ROM. Must be freed.
 */
uint8_t *read_rom_to_ram(const char *file_name)
{
	FILE *rom_file = fopen(file_name, "rb");
	size_t rom_size;
	uint8_t *rom = NULL;

	if(rom_file == NULL)
		return NULL;

	fseek(rom_file, 0, SEEK_END);
	rom_size = ftell(rom_file);
	fseek(rom_file, 0, SEEK_SET);
	rom = malloc(rom_size);

	if(fread(rom, sizeof(uint8_t), rom_size, rom_file) != rom_size)
	{
		if(rom != NULL) free(rom);
		fclose(rom_file);
		return NULL;
	}

	fclose(rom_file);
	return rom;
}

void read_cart_ram_file(const char *save_file_name, uint8_t **dest,
			const size_t len)
{
	FILE *f;

	/* If save file not required. */
	if(len == 0)
	{
		*dest = NULL;
		return;
	}

	/* Allocate enough memory to hold save file. */
	if((*dest = realloc(NULL, len)) == NULL)
	{
		printf("Unable to allocate save file memory.\n");
	}

	f = fopen(save_file_name, "rb");
	/* It doesn't matter if the save file doesn't exist. We initialise the
	 * save memory allocated above. The save file will be created on exit. */
	if(f == NULL)
	{
		memset(*dest, 0, len);
		return;
	}

	/* Read save file to allocated memory. */
	fread(*dest, sizeof(uint8_t), len, f);
	fclose(f);
}

void write_cart_ram_file(const char *save_file_name, uint8_t **dest,
			 const size_t len)
{
	FILE *f;

	if(len == 0 || *dest == NULL)
		return;

	if((f = fopen(save_file_name, "wb")) == NULL)
	{
		printf("Unable to open save file.\n");
	}

	/* Record save file. */
	fwrite(*dest, sizeof(uint8_t), len, f);
	fclose(f);
}

/**
 * Handles an error reported by the emulator. The emulator context may be used
 * to better understand why the error given in gb_err was reported.
 */
void gb_error(struct gb_s *gb, const enum gb_error_e gb_err, const uint16_t val)
{
	switch(gb_err)
	{
	case GB_INVALID_OPCODE:
		/* We compensate for the post-increment in the __gb_step_cpu
		 * function. */
		printf("Invalid opcode\n");
		break;

	/* Ignoring non fatal errors. */
	case GB_INVALID_WRITE:
	case GB_INVALID_READ:
		return;

	default:
		printf("Unknown error\n");
		break;
	}

	return;
}

#if ENABLE_LCD
/**
 * Draws scanline into framebuffer.
 */
void lcd_draw_line(struct gb_s *gb, const uint8_t* pixels,
		   const uint_fast8_t line)
{
	struct priv_t *priv = gb->direct.priv;

	for(unsigned int x = 0; x < LCD_WIDTH; x++)
	{
		priv->fb[line][x] = pixels[x] & 3;
	}
}
#endif

void saveGame() {
    if(!is_rom_chosen) return;
    write_cart_ram_file(save_file_name, &priv.cart_ram, gb_get_save_size(&gb));
}

void reset() {
    if(!is_rom_chosen) return;
    gb_reset(&gb);
}

void loadRom() {
    /* Copy input ROM file to allocated memory. */
    if((priv.rom = read_rom_to_ram(rom_file_name)) == NULL)
    {
        printf("Error loading ROM\n");
        out();
    }

    /* If no save file is specified, copy save file (with specific name) to
     * allocated memory. */
    if(save_file_name == NULL)
    {
        char *str_replace;
        const char extension[] = ".sav";

        /* Allocate enough space for the ROM file name, for the "sav"
         * extension and for the null terminator. */
        save_file_name = realloc(NULL, strlen(rom_file_name) + strlen(extension) + 1);

        if(save_file_name == NULL)
        {
            printf("Error with save file name\n");
            out();
        }

        /* Copy the ROM file name to allocated space. */
        strcpy(save_file_name, rom_file_name);

        /* If the file name does not have a dot, or the only dot is at
         * the start of the file name, set the pointer to begin
         * replacing the string to the end of the file name, otherwise
         * set it to the dot. */
        if((str_replace = strrchr(save_file_name, '.')) == NULL ||
           str_replace == save_file_name)
            str_replace = save_file_name + strlen(save_file_name);

        /* Copy extension to string including terminating null byte. */
        for(unsigned int i = 0; i <= strlen(extension); i++)
            *(str_replace++) = extension[i];
    }

    /* Initialise emulator context. */
    gb_ret = gb_init(&gb, &gb_rom_read, &gb_cart_ram_read, &gb_cart_ram_write,
                     &gb_error, &priv);

    switch(gb_ret)
    {
        case GB_INIT_NO_ERROR:
            break;

        case GB_INIT_CARTRIDGE_UNSUPPORTED:
            printf("Unsupported cartridge.\n");
            out();

        case GB_INIT_INVALID_CHECKSUM:
            printf("Invalid ROM: Checksum failure.\n");
            out();

        default:
            printf("Unknown error: %d\n", gb_ret);
            out();
    }

    /* Load Save File. */
    read_cart_ram_file(save_file_name, &priv.cart_ram, gb_get_save_size(&gb));
}

void chooseRom() {
    if((rom_file_name = rom_list_update()) == NULL) return;
    else {
        is_rom_chosen = 1;

        // Clear the framebuffer
        memset(fbp, 0, screensize);

        loadRom();

    #if defined(ENABLE_SOUND_MINIGB)
            gameSoundSource = pd->sound->addSource(audio_callback_playdate, NULL, 1);

            audio_init();
    #endif

        gb_init_lcd(&gb, &lcd_draw_line);
    }
}

void loadNewRom() {
    if(!is_rom_chosen) return;
    saveGame();
    gb_reset(&gb);
    if(priv.rom != NULL) free(priv.rom);
    if(priv.cart_ram != NULL) free(priv.cart_ram);
    if(save_file_name != NULL) free(save_file_name);
    if(rom_file_name != NULL) free(rom_file_name);
    rom_file_name = NULL;
    save_file_name = NULL;
#if defined(ENABLE_SOUND_MINIGB)
    pd->sound->channel->removeSource(pd->sound->getDefaultChannel(), gameSoundSource);
#endif
    redraw_menu_screen();
    is_rom_chosen = 0;
}

int eventHandler()
{
    int retval = 0;
    unsigned long startSelectDelay = 30;
    float crankChange = 0.0;

    while (1) {
	    
        // Get Linux button state
	// fd = input device, ev = input event
	fd = open("/dev/input/event0", O_RDONLY);
        while (read(fd, &ev, sizeof(struct input_event)) > 0) {
            if (ev.type == EV_KEY) {
                if (ev.code == KEY_J) {
                    gb.direct.joypad_bits.a = (ev.value == 0) ? 1 : 0;
                }
                else if (ev.code == KEY_K) {
                    gb.direct.joypad_bits.b = (ev.value == 0) ? 1 : 0;
                }
                else if (ev.code == KEY_ENTER) {
                    gb.direct.joypad_bits.start = (ev.value == 0) ? 1 : 0;
                }
		else if (ev.code == KEY_SPACE) {
                    gb.direct.joypad_bits.select = (ev.value == 0) ? 1 : 0;
                }
		else if (ev.code == KEY_UP || ev.code == KEY_W) {
                    gb.direct.joypad_bits.up = (ev.value == 0) ? 1 : 0;
                }
                else if (ev.code == KEY_DOWN || ev.code == KEY_S) {
                    gb.direct.joypad_bits.down = (ev.value == 0) ? 1 : 0;
                }
                else if (ev.code == KEY_LEFT || ev.code == KEY_A) {
                    gb.direct.joypad_bits.left = (ev.value == 0) ? 1 : 0;
                }
                else if (ev.code == KEY_RIGHT || ev.code == KEY_D) {
                    gb.direct.joypad_bits.right = (ev.value == 0) ? 1 : 0;
                }
            }
        }

        /* Execute CPU cycles until the screen has to be redrawn. */
        gb_run_frame(&gb);

        if(gb.direct.frame_skip && gb.display.frame_skip_count) {
            uint16_t xx = 40;
            uint16_t yy = -1;
            uint8_t yyRowSkip = 0;
            uint16_t fbIndex = 0;
            uint16_t fbIndex2 = 0;
            for (uint8_t y = 0; y < LCD_HEIGHT - 1; ++y) {
                if ((y << 1) % 6 == 0) {
                    yyRowSkip = 1;
                }
                xx = 40;
                yy += 2 - yyRowSkip;
                fbIndex = yy * LCD_ROWSIZE + (xx >> 3);
                fbIndex2 = fbIndex + 52;
                for (uint8_t x = 0; x < LCD_WIDTH; ++x) {
                    switch (priv.fb[y][x]) {
                        case 0: // white
                            FrameBuffer[fbIndex] |= (3 << (6 - (xx & 7)));
                            FrameBuffer[fbIndex2] |= (3 << (6 - (xx & 7)));
                            break;
                        case 1: // light gray
                                if ((yy & 1) == 0) {
                                    FrameBuffer[fbIndex] |= (1 << (7 - (xx & 7)));
                                    FrameBuffer[fbIndex] &= ~(1 << (7 - ((xx+1) & 7)));
                                    FrameBuffer[fbIndex2] |= (3 << (6 - (xx & 7)));
                                }
                                else {
                                    FrameBuffer[fbIndex] |= (3 << (6 - (xx & 7)));
                                    FrameBuffer[fbIndex2] |= (1 << (7 - (xx & 7)));
                                    FrameBuffer[fbIndex2] &= ~(1 << (7 - ((xx+1) & 7)));
                                }
                            break;
                        case 2: // dark gray
                                if ((yy & 1) == 0) {
                                    FrameBuffer[fbIndex] |= (1 << (7 - (xx & 7)));
                                    FrameBuffer[fbIndex] &= ~(1 << (7 - ((xx+1) & 7)));
                                    FrameBuffer[fbIndex2] &= ~(1 << (7 - (xx & 7)));
                                    FrameBuffer[fbIndex2] |= (1 << (7 - ((xx+1) & 7)));

                                }
                                else {
                                    FrameBuffer[fbIndex] &= ~(1 << (7 - (xx & 7)));
                                    FrameBuffer[fbIndex] |= (1 << (7 - ((xx+1) & 7)));
                                    FrameBuffer[fbIndex2] |= (1 << (7 - (xx & 7)));
                                    FrameBuffer[fbIndex2] &= ~(1 << (7 - ((xx+1) & 7)));
                                }
                            break;
                        case 3: // black
                            FrameBuffer[fbIndex] &= ~(3 << (6 - (xx & 7)));
                            FrameBuffer[fbIndex2] &= ~(3 << (6 - (xx & 7)));
                            break;
                    }

                    xx += 2;
                    if((xx & 7) == 0) { fbIndex += 1; fbIndex2 += 1; };
                }
                yyRowSkip = 0;
            }

            // Update the Linux framebuffer with the modified FrameBuffer data
            memcpy(fbp, FrameBuffer, screensize);
        }

        // Sleep for a short duration to control the frame rate
        usleep(1000);
    }

    return retval;
}

void quit() {
    /* Record save file. */
    saveGame();
    out();
}

void out() {
    if(priv.rom != NULL) free(priv.rom);
    if(priv.cart_ram != NULL) free(priv.cart_ram);
    if(save_file_name != NULL) free(save_file_name);
    if(rom_file_name != NULL) free(rom_file_name);

    rom_list_cleanup();

    // Unmap the framebuffer memory and close the framebuffer device
    munmap(fbp, screensize);
    close(fbfd);

    // Close the input device
    close(fd);
}

int main() {
    fbfd = open(FRAMEBUFFER_DEVICE, O_RDWR);
    if (fbfd == -1) {
        printf("Error opening framebuffer device.\n");
        return 1;
    }

    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        printf("Error reading fixed information.\n");
        return 1;
    }

    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        printf("Error reading variable information.\n");
        return 1;
    }

    screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

    fbp = (char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbp == MAP_FAILED) {
        printf("Error mapping framebuffer memory.\n");
        return 1;
    }

    FrameBuffer = malloc(screensize);

    // Initialize the framebuffer with initial data

    // Call eventHandler to start the game loop
    eventHandler();

    return 0;
}
