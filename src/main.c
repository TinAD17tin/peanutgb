#include <eadk.h>
#include <stdlib.h>
#include "peanut_gb/peanut_gb.h"
#include <stdio.h>
#include <string.h>


const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Game Boy";
const uint32_t eadk_api_level  __attribute__((section(".rodata.eadk_api_level"))) = 0;
uint_fast32_t offset = 0;
uint_fast32_t ramAddr = 0;
uint_fast32_t ramSize = 0;
int ret = 0;

uint8_t gb_rom_read(struct gb_s* gb, const uint_fast32_t addr) {
    uint8_t* pack = (uint8_t*)eadk_external_data;
    //read pack from offset
    return pack[offset + addr];
}

void gb_cart_ram_write(struct gb_s* gb, const uint_fast32_t addr, const uint8_t val) {

}

uint8_t gb_cart_ram_read(struct gb_s* gb, const uint_fast32_t addr) {
    return 0x00;

}

void gb_error(struct gb_s* gb, const enum gb_error_e gb_err, const uint16_t val) {
    //  printf("GB_ERROR %d %d %d\n", gb_err, GB_INVALID_WRITE, val);
    return;
}

eadk_color_t palette_original[4] = { 0x8F80, 0x24CC, 0x4402, 0x0A40 };
eadk_color_t palette_gray[4] = { eadk_color_white, 0xAD55, 0x52AA, eadk_color_black };

eadk_color_t* palette = palette_original;

eadk_color_t eadk_color_from_gb_pixel(uint8_t gb_pixel) {
    uint8_t gb_color = gb_pixel & 0x3;
    return palette[gb_color];
}



static void lcd_draw_line_centered(struct gb_s* gb, const uint8_t* input_pixels, const uint_fast8_t line) {
    eadk_color_t output_pixels[LCD_WIDTH];
    eadk_point_t point = { 0, line };

    //fix each pixel with gb->cgb.fixPalette
    for (int i = 0; i < LCD_WIDTH; i++) {
        if (gb->cgb.cgbMode) {
            output_pixels[i] = gb->cgb.fixPalette[input_pixels[i]];
            output_pixels[i] = (output_pixels[i] & 0x1F) << 11 | (output_pixels[i] & 0x3E0) << 1 | (output_pixels[i] & 0x7C00) >> 10;
            output_pixels[i] = (output_pixels[i] & 0x1F) << 11 | (output_pixels[i] & 0x7E0) | (output_pixels[i] & 0xF800) >> 11;
        }
        else {
            output_pixels[i] = eadk_color_from_gb_pixel(input_pixels[i]);
        }
    }
    //dump output_pixels to screen
    eadk_display_push_rect((eadk_rect_t) { (EADK_SCREEN_WIDTH - LCD_WIDTH) / 2, (EADK_SCREEN_HEIGHT - LCD_HEIGHT) / 2 + line, LCD_WIDTH, 1 }, output_pixels);

}

static void lcd_draw_line_maximized(struct gb_s* gb, const uint8_t* input_pixels, const uint_fast8_t line) {
    // Nearest neighbor scaling of a 160x144 texture to a 320x240 resolution
    // Horizontally, we just double
    eadk_color_t output_pixels[LCD_WIDTH];
    eadk_color_t zoomPixels[2 * LCD_WIDTH];
    for (int i = 0; i < LCD_WIDTH; i++) {
        if (gb->cgb.cgbMode) {
            output_pixels[i] = gb->cgb.fixPalette[input_pixels[i]];
            output_pixels[i] = (output_pixels[i] & 0x1F) << 11 | (output_pixels[i] & 0x3E0) << 1 | (output_pixels[i] & 0x7C00) >> 10;
            output_pixels[i] = (output_pixels[i] & 0x1F) << 11 | (output_pixels[i] & 0x7E0) | (output_pixels[i] & 0xF800) >> 11;
        }
        else {
            output_pixels[i] = eadk_color_from_gb_pixel(input_pixels[i]);
        }

        eadk_color_t color = output_pixels[i];


        zoomPixels[2 * i] = color;
        zoomPixels[2 * i + 1] = color;
    }
    // Vertically, we want to scale by a 5/3 ratio. So we need to make 5 lines out of three:  we double two lines out of three.
    uint16_t y = (5 * line) / 3;
    eadk_display_push_rect((eadk_rect_t) { 0, y, 2 * LCD_WIDTH, 1 }, zoomPixels);
    if (line % 3 != 0) {
        eadk_display_push_rect((eadk_rect_t) { 0, y + 1, 2 * LCD_WIDTH, 1 }, zoomPixels);
    }

}

struct gb_s gb;

int _getpid(void) { return 1; }
int _kill(int pid, int sig) { return 0; }
int _fstat(int fd, struct stat* st) { return 0; }
int _isatty(int fd) { return 0; }
void _exit(int status) { while (1) {} }
void _sbrk(int incr) { return 0; }

int main(int argc, char* argv[]) {
    eadk_timing_msleep(400);
    int usableFiles = 0;
    uint8_t* pack = (uint8_t*)eadk_external_data;
    eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);

    unsigned int packSize, filesCount;
    //read the first 4 bytes of the pack as the pack size
    memcpy(&packSize, pack, 4);
    //read the next 4 bytes of the pack as the files count
    memcpy(&filesCount, pack + 4, 4);
    //read pointer jungle
    uint8_t* filePointers = pack + 0x10;
    char pointerJungle[0x40];
    memcpy(pointerJungle, filePointers, 0x40);


    char** fileNames = malloc(sizeof(char*) * filesCount);
    unsigned int* fileOffsets = malloc(sizeof(unsigned int) * filesCount);
    int selectedFile = 0;

    //read file names and offsets and push into a select menu
    for (int i = 0; i < filesCount; i++) {
        //read file address
        unsigned int fileAddress;
        fileAddress = *(unsigned int*)(pointerJungle + i * 4);

        //read file header
        //load var with pack + fileAddress
        uint8_t* fileHere = pack + fileAddress;
        char fileHeader[0xD0];
        memcpy(fileHeader, fileHere, 0xD0);

        //add file name and offset to array
        //offset var is fileAddress + D0
        unsigned int offsetFile = fileAddress + 0xD0;
        char* fileName = malloc(128);
        memcpy(fileName, fileHeader, 128);
        char* extension = strrchr(fileName, '.');
        if (extension != NULL && (strcmp(extension, ".gb") == 0 || strcmp(extension, ".gbc") == 0)) {
            fileNames[usableFiles] = strdup(fileName);
            fileOffsets[usableFiles] = offsetFile;
            usableFiles++;
        }
    }


    eadk_keyboard_state_t kbd = eadk_keyboard_scan();
    char* str = (char*)malloc(100);

    eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);
    for (int i = 0; i < usableFiles; i++) {
        if (i == selectedFile) {
            sprintf(str, "> %s", fileNames[i]);
        }
        else {
            sprintf(str, "  %s", fileNames[i]);
        }
        eadk_display_draw_string(str, (eadk_point_t) { 0, i * 10 }, 0, eadk_color_white, eadk_color_black);
    }


    while (offset == 0) {
        kbd = eadk_keyboard_scan();
        int len = usableFiles;

        if (eadk_keyboard_key_down(kbd, eadk_key_up)) {
            while (eadk_keyboard_key_down(kbd, eadk_key_up)) {
                kbd = eadk_keyboard_scan();
            }

            //if up is pressed, decrement selected file
            selectedFile--;
            //if selected file is less than 0, set selected file to 0
            if (selectedFile < 0) {
                selectedFile = len - 1;
            }
            eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);
            for (int i = 0; i < len; i++) {
                if (i == selectedFile) {
                    sprintf(str, "> %s", fileNames[i]);
                }
                else {
                    sprintf(str, "  %s", fileNames[i]);
                }
                eadk_display_draw_string(str, (eadk_point_t) { 0, i * 10 }, 0, eadk_color_white, eadk_color_black);
            }


        }
        //check down press
        if (eadk_keyboard_key_down(kbd, eadk_key_down)) {
            while (eadk_keyboard_key_down(kbd, eadk_key_down)) {
                kbd = eadk_keyboard_scan();
            }
            //if down is pressed, increment selected file
            selectedFile++;
            //if selected file is greater than filesCount, set selected file to filesCount
            if (selectedFile >= len) {
                selectedFile = 0;
            }
            eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);
            for (int i = 0; i < len; i++) {
                if (i == selectedFile) {
                    sprintf(str, "> %s", fileNames[i]);
                }
                else {
                    sprintf(str, "  %s", fileNames[i]);
                }
                eadk_display_draw_string(str, (eadk_point_t) { 0, i * 10 }, 0, eadk_color_white, eadk_color_black);
            }
        }
        //check enter press
        if (eadk_keyboard_key_down(kbd, eadk_key_exe) || eadk_keyboard_key_down(kbd, eadk_key_ok)) {
            //if enter is pressed, set offset to the selected file offset
            offset = fileOffsets[selectedFile];
        }
    }

    // Clean up
    for (int i = 0; i < filesCount; i++) {
        free(fileNames[i]);
    }
    free(fileNames);
    free(fileOffsets);
    free(str);
    //initGB

    ret = gb_init(&gb, gb_rom_read, gb_cart_ram_read, gb_cart_ram_write, gb_error, NULL);
    if (ret != GB_INIT_NO_ERROR) {
        return -1;
    }

    gb_init_lcd(&gb, lcd_draw_line_maximized);

    while (true) {
        gb_run_frame(&gb);

        eadk_keyboard_state_t kbd = eadk_keyboard_scan();
        gb.direct.joypad_bits.a = !eadk_keyboard_key_down(kbd, eadk_key_back);
        gb.direct.joypad_bits.b = !eadk_keyboard_key_down(kbd, eadk_key_ok);
        gb.direct.joypad_bits.select = !eadk_keyboard_key_down(kbd, eadk_key_shift);
        gb.direct.joypad_bits.start = !eadk_keyboard_key_down(kbd, eadk_key_backspace);
        gb.direct.joypad_bits.right = !eadk_keyboard_key_down(kbd, eadk_key_right);
        gb.direct.joypad_bits.left = !eadk_keyboard_key_down(kbd, eadk_key_left);
        gb.direct.joypad_bits.up = !eadk_keyboard_key_down(kbd, eadk_key_up);
        gb.direct.joypad_bits.down = !eadk_keyboard_key_down(kbd, eadk_key_down);

        if (eadk_keyboard_key_down(kbd, eadk_key_one)) {
            palette = palette_original;
        }
        if (eadk_keyboard_key_down(kbd, eadk_key_two)) {
            palette = palette_gray;
        }
        if (eadk_keyboard_key_down(kbd, eadk_key_plus)) {
            gb.display.lcd_draw_line = lcd_draw_line_maximized;
        }
        if (eadk_keyboard_key_down(kbd, eadk_key_minus)) {
            eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);
            gb.display.lcd_draw_line = lcd_draw_line_centered;
        }
    }

    return 0;
}