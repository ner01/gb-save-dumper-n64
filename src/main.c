/*
* Copyright (c) Krikzz and Contributors.
* See LICENSE file in the project root for full license information.
*/

#include "everdrive.h"
#include "libgbpak.h"
#include "errcodes.h"

/*
8MB ROM and 128KB RAM are the max supported by libgbpak
The N64 only has 4MB RAM (without expansion pak), so creating a buffer much larger than 2MB won't work.
Need to stream the data out somewhere to support the subset of games that are >2MB.
*/
#define BUFFER_SIZE 1024 * 1024 * 2

bool main_display_write_ram();
void main_display_edid();
void main_display_credits();
void main_display_error(u8 err);
u8 main_display_menu();

extern cart gbcart;

/*
 Holds the last operation performed. Used to display relevant completion messages to the user.
 Set to NONE when the user cancels an operation.
*/
typedef enum
{
    NONE = 0,
    READ_RAM
} ops_t;

err_t res = ERR_NONE;
ops_t op = NONE;
/*used to limit displaying cartridge metadata only when init_gbpak is successful*/
uint8_t cartDataGood = FALSE;
unsigned sum = 0; //simple checksum
uint8_t buffer[BUFFER_SIZE] = {0};

void printMapper(void)
{
    switch (gbcart.mapper)
    {
    case GB_NORM:
        screen_append_str_print("none");
        break;
    case GB_MBC1:
        screen_append_str_print("MBC1");
        break;
    case GB_MBC2:
        screen_append_str_print("MBC2");
        break;
    case GB_MMMO1:
        screen_append_str_print("MMM01");
        break;
    case GB_MBC3:
        screen_append_str_print("MBC3");
        break;
    case GB_MBC5:
        screen_append_str_print("MBC5");
        break;
    case GB_CAMERA:
        screen_append_str_print("(Camera)");
        break;
    case GB_TAMA5:
        screen_append_str_print("TAMA5");
        break;
    case GB_HUC3:
        screen_append_str_print("HUC3");
        break;
    case GB_HUC1:
        screen_append_str_print("HUC1");
        break;
    default:
        screen_append_str_print("unknown");
        break;
    }
}

void printCartData()
{
    if (!cartDataGood)
        return;

    screen_print("");
    screen_append_str_print("Title: ");
    screen_append_str_print(gbcart.title);
    screen_print("");
    screen_append_str_print("Mapper: ");
    printMapper();
    screen_print("");
    screen_append_str_print("ROM Size: ");
    screen_append_hex32_print(gbcart.romsize);
    screen_print("");
    screen_append_str_print("RAM Size: ");
    screen_append_hex32_print(gbcart.ramsize);
}

void printError()
{
    switch (res)
    {
    case ERR_GB_INIT:
        screen_print("Error connecting to Transfer Pak.");
        return;
    case ERR_RAM_READ:
        screen_print("Error reading cart RAM.");
        return;
    case ERR_FILE_OPEN:
        screen_print("Error opening file system");
        return;
    case ERR_FILE_WRITE:
        screen_print("Error writing file");
        return;
    case ERR_FILE_CLOSE:
        screen_print("Error closing file system");
        return;
    default:
        break;
    }
}

void simpleChecksum(int size)
{
    sum = 0;
    for (unsigned i = 0; i < size; i++)
    {
        sum += buffer[i];
    }
}

int main(void)
{
    u8 resp;
    FATFS fs;

    sysInit();
    bi_init();

    screen_clear();
    screen_print("File system initilizing...");
    screen_repaint();

    /* mount disk */
    memset(&fs, 0, sizeof(fs));
    resp = f_mount(&fs, "", 1);
    if (resp)
        main_display_error(resp);

    while (1)
    {
        resp = main_display_menu();
        if (resp)
            main_display_error(resp);
    }
}

u8 main_display_menu()
{
    enum
    {
        MENU_FILE_WRITE,
        MENU_EDID,
        MENU_CREDITS,
        MENU_SIZE
    };

    struct controller_data cd;
    u8 *menu[MENU_SIZE];
    u32 selector = 0;
    u8 resp;

    menu[MENU_FILE_WRITE] = "Save GB RAM to SD card";
    menu[MENU_EDID] = "ED64 Hardware Info";
    menu[MENU_CREDITS] = "Credits";

    while (1)
    {

        screen_clear();
        screen_print("Ner0's GameBoy Save Dumper");
        screen_print(" ");

        for (int i = 0; i < MENU_SIZE; i++)
        {
            screen_print("  ");
            if (i == selector)
            {
                screen_append_str_print(">");
            }
            else
            {
                screen_append_str_print(" ");
            }
            screen_append_str_print(menu[i]);
        }

        screen_repaint();
        controller_scan();
        cd = get_keys_down();

        if (cd.c[0].up)
        {
            if (selector != 0)
                selector--;
        }

        if (cd.c[0].down)
        {
            if (selector < MENU_SIZE - 1)
                selector++;
        }

        if (!cd.c[0].A)
            continue;

        /* write srm to the game.sram file */
        if (selector == MENU_FILE_WRITE)
        {
            if (main_display_write_ram())
            {
                printError();
            }
        }

        /* everdrive hardware identification */
        if (selector == MENU_EDID)
        {
            main_display_edid();
        }

        /* show credits */
        if (selector == MENU_CREDITS)
        {
            main_display_credits();
        }
    }
}

bool main_display_write_ram()
{
    struct controller_data cd;
    int retval = 0;

    screen_clear();
    screen_print("Ner0's GameBoy Save Dumper");
    screen_print(" ");
    op = READ_RAM;
    res = ERR_NONE;

    retval = init_gbpak();
    if (retval)
    {
        res = ERR_GB_INIT;
        cartDataGood = FALSE;
        return true;
    }
    cartDataGood = TRUE;
    printCartData();

    screen_print("");
    screen_print("Press (A) to dump GB Ram");
    screen_print("Press (B) to return to main menu");
    screen_repaint();
    while (1)
    {
        screen_vsync();
        controller_scan();
        cd = get_keys_down();

        if (cd.c[0].A)
        {
            screen_print("Copying data to SD...");
            screen_repaint();
            retval = copy_gbRam_toRAM(buffer);
            if (retval)
            {
                res = ERR_RAM_READ;
                return true;
            }

            simpleChecksum(gbcart.ramsize);

            const char *title = gbcart.title;
            const char *extension = ".srm";

            u8 *filename;
            filename = malloc(strlen(title) + 1 + 4);
            strcpy(filename, title);
            strcat(filename, extension);

            FIL f;
            UINT bw;
            u8 resp;
            u32 ramSize = gbcart.ramsize;

            screen_clear();
            screen_repaint();

            resp = f_open(&f, filename, FA_WRITE | FA_CREATE_ALWAYS);
            if (resp)
            {
                res = ERR_FILE_OPEN;
                return true;
            }

            resp = f_write(&f, buffer, ramSize, &bw);
            if (resp)
            {
                res = ERR_FILE_WRITE;
                return true;
            }

            resp = f_close(&f);
            if (resp)
            {
                res = ERR_FILE_CLOSE;
                return true;
            }

            screen_print("Sucessfully written RAM to the file: ");
            screen_print("\"SD:/");
            screen_append_str_print(filename);
            screen_append_str_print("\"");
            screen_print("");
            screen_print("");
            screen_print("");
            screen_print("Press (B) to return to main menu");

            screen_repaint();
            while (1)
            {
                screen_vsync();
                controller_scan();
                cd = get_keys_down();

                if (cd.c[0].B)
                {
                    break;
                }
            }

            return false;
        }

        if (cd.c[0].B)
        {
            return false;
        }
    }
}

void main_display_edid()
{

    struct controller_data cd;
    u32 id = bi_get_cart_id();

    screen_clear();
    screen_print("ED64 H/W Rev ID:   ");
    screen_append_hex32_print(id);
    screen_print("ED64 H/W Rev Name: ");

    switch (id)
    {
    case CART_ID_V2:
        screen_append_str_print("EverDrive 64 V2.5");
        break;
    case CART_ID_V3:
        screen_append_str_print("EverDrive 64 V3");
        break;
    case CART_ID_X7:
        screen_append_str_print("EverDrive 64 X7");
        break;
    case CART_ID_X5:
        screen_append_str_print("EverDrive 64 X5");
        break;
    default:
        screen_append_str_print("Unknown");
        break;
    }

    screen_print("");
    screen_print("Press (B) to return to main menu");
    screen_repaint();
    while (1)
    {
        screen_vsync();
        controller_scan();
        cd = get_keys_down();

        if (cd.c[0].B)
        {
            break;
        }
    }
}

void main_display_credits()
{
    struct controller_data cd;

    screen_clear();
    screen_print("Special Thanks to");
    screen_print(" ");
    screen_print("saturnu");
    screen_print("ragnaroktomorrow");
    screen_print("networkfusion");
    screen_print("krikzz");
    screen_print(" ");
    screen_print("For updates visit: ");
    screen_print("github.com/ner01/gb-save-dumper-n64");
    screen_print("");
    screen_print("Press (B) to return to main menu");
    screen_repaint();
    while (1)
    {
        screen_vsync();
        controller_scan();
        cd = get_keys_down();

        if (cd.c[0].B)
        {

            break;
        }
    }
}

void main_display_error(u8 err)
{

    screen_clear();
    screen_print("error: ");
    screen_append_hex8_print(err);
    screen_repaint();

    while (1)
        ;
}
