

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "keyboard_gii39.h"
#include "task.h"

// #include "lfs.h"

#include "SystemUI.h"
#include "sys_llapi.h"
extern const unsigned char VGA_Ascii_5x8[];
extern const unsigned char VGA_Ascii_6x12[];
extern const unsigned char VGA_Ascii_8x16[];
#ifdef __cplusplus
extern "C" {
#endif
#include "SysConf.h"
#include "SystemFs.h"

void memtest(uint32_t testSize);
void ll_disp_put_area(uint8_t *vbuffer, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);

class ReadDisplay {
public:
    static const int KEY_TRIG = 0;
    static const int KEY_LONG_PRESS = 1;
    static const int KEY_RELEASE = 2;

private:
    uint8_t *disp_buf;
    int disp_w, disp_h;
    void (*drawf)(uint8_t *buf, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
    inline void buf_set(uint32_t x, uint32_t y, uint8_t c) {
        if (disp_buf) {
            if ((x < this->disp_w) && (y < this->disp_h))
                this->disp_buf[x + y * this->disp_w] = c;
        }
    }

public:
    ReadDisplay(int display_width, int display_height, void (*drawf)(uint8_t *buf, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)) {
        printf("Create UI Display.\n");
        this->disp_buf = (uint8_t *)pvPortMalloc(display_width * display_height);
        this->drawf = drawf;
        this->disp_w = display_width;
        this->disp_h = display_height;
        memset(this->disp_buf, 0xff, display_width * display_height);
        this->drawf(this->disp_buf, 0, 0, this->disp_w - 1, this->disp_h - 1);
    }

    void emergencyBuffer() {
        disp_buf = (uint8_t *)(RAM_BASE + BASIC_RAM_SIZE - 33 * 1024);
    }

    void releaseBuffer() {
        if (disp_buf) {
            vPortFree(disp_buf);
            disp_buf = NULL;
        }
    }

    void restoreBuffer() {
        if (!disp_buf) {
            this->disp_buf = (uint8_t *)pvPortMalloc(disp_w * disp_h);
        }
    }

    void draw_point(uint32_t x, uint32_t y, uint8_t c) {
        buf_set(x, y, c);
        this->drawf(&this->disp_buf[y * this->disp_w], 0, y, this->disp_w - 1, y);
    }
    void draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint8_t c) {
        if (y0 == y1) {
            for (int x = x0; x <= x1; x++) {
                buf_set(x, y0, c);
            }
            goto draw_fin;
        }
        if (x0 == x1) {
            for (int y = y0; y <= y1; y++) {
                buf_set(x0, y, c);
            }
            goto draw_fin;
        }

        int x, y, dx, dy, e;
        dx = x1 - x0;
        dy = y1 - y0;
        e = -dx;
        x = x0;
        y = y0;
        for (int i = 0; i < dx; i++) {
            buf_set(x, y, c);
            x++;
            e += 2 * dy;
            if (e >= 0) {
                y++;
                e -= 2 * dx;
            }
        }
    draw_fin:
        this->drawf(&this->disp_buf[y0 * this->disp_w], 0, y0, this->disp_w - 1, y1);
    }
    void draw_box(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, int16_t borderColor, int16_t fillColor) {
        if (fillColor != -1) {
            for (int y = y0; y <= y1; y++) {
                draw_line(x0, y, x1, y, fillColor);
            }
        }
        if (borderColor != -1) {
            draw_line(x0, y0, x1, y0, borderColor);
            draw_line(x0, y1, x1, y1, borderColor);

            draw_line(x0, y0, x0, y1, borderColor);
            draw_line(x1, y0, x1, y1, borderColor);
        }
    }

    void draw_bmp(char *src, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                buf_set(x0 + x, y0 + y, src[x + y * w]);
            }
        }

        this->drawf(&this->disp_buf[y0 * this->disp_w], 0, y0, this->disp_w - 1, y0 + h);
    }

    void draw_char_ascii(uint32_t x0, uint32_t y0, char ch, uint8_t fontSize, uint8_t fg, int16_t bg) {
        int font_w;
        int font_h;
        const unsigned char *pCh;
        unsigned int x = 0, y = 0, i = 0, j = 0;

        if ((ch < ' ') || (ch > '~' + 1)) {
            return;
        }

        switch (fontSize) {
        case 8:
            font_w = 8;
            font_h = 8;
            pCh = VGA_Ascii_5x8 + (ch - ' ') * font_h;
            break;

        case 12:
            font_w = 8;
            font_h = 12;
            pCh = VGA_Ascii_6x12 + (ch - ' ') * font_h;
            break;

        case 16:
            font_w = 8;
            font_h = 16;
            pCh = VGA_Ascii_8x16 + (ch - ' ') * font_h;
            break;

        default:
            return;
        }
        char pix;
        while (y < font_h) {
            while (x < font_w) {
                pix = ((*pCh << x) & 0x80U);
                if (pix) {
                    buf_set(x0 + x, y0 + y, fg);
                } else {
                    if (bg != -1) {
                        buf_set(x0 + x, y0 + y, bg);
                    }
                }
                x++;
            }
            x = 0;
            y++;
            pCh++;
        }

        this->drawf(&this->disp_buf[y0 * this->disp_w], 0, y0, this->disp_w - 1, y0 + font_h - 1);
    }
    void draw_char_GBK16(uint32_t x0, uint32_t y0, uint16_t c, uint8_t fg, int16_t bg) {
        extern uint32_t fonts_hzk_start;
        extern uint32_t fonts_hzk_end;
        int lv = (c & 0xFF) - 0xa1;
        int hv = (c >> 8) - 0xa1;
        uint32_t offset = (uint32_t)(94 * hv + lv) * 32;
        uint8_t *font_data = (uint8_t *)(((uint32_t)&fonts_hzk_start) + offset);
        if ((uint32_t)font_data > (uint32_t)&fonts_hzk_end) {
            return;
        }

        int x = x0, y = y0;

        for (int i = 0; i < 32; i += 2) {
            uint8_t pix;
            for (int t = 0, pix = font_data[i]; t < 8; t++) {
                if (pix & 0x80) {
                    buf_set(x, y, fg);
                } else {
                    if (bg != -1)
                        buf_set(x, y, bg);
                }
                ++x;
                pix <<= 1;
            }

            for (int t = 0, pix = font_data[i + 1]; t < 8; t++) {
                if (pix & 0x80) {
                    buf_set(x, y, fg);
                } else {
                    if (bg != -1)
                        buf_set(x, y, bg);
                }
                ++x;
                pix <<= 1;
            }

            x = x0;
            y++;
        }
        // printf("GBK PRINT:%02x\n", c);
        this->drawf(&this->disp_buf[y0 * this->disp_w], 0, y0, this->disp_w - 1, y0 + 16);
    }
    int draw_printf(uint32_t x0, uint32_t y0, uint8_t fontSize, uint8_t fg, int16_t bg, const char *format, ...) {
        va_list aptr;
        int ret;

        char buffer[256];

        va_start(aptr, format);
        ret = vsprintf(buffer, format, aptr);
        va_end(aptr);

        for (int i = 0, x = x0; (i < sizeof(buffer)) && (buffer[i]); i++) {
            if (buffer[i] < 0x80) {
                draw_char_ascii(x, y0, buffer[i], fontSize, fg, bg);
                x += fontSize == 16 ? 8 : 6;
                if (x > disp_w) {
                    break;
                }
            } else {
                draw_char_GBK16(x, y0, buffer[i + 1] | (buffer[i] << 8), fg, bg);
                x += 16;
                if (x > disp_w) {
                    break;
                }
                i++;
            }
        }
        return (ret);
    }

    bool keyMsg(uint32_t key, int state) {
        switch (state) {
        case KEY_RELEASE:
            if (key == KEY_ON)
                return false;
        default:
            return true;
        }
    };

    ~ReadDisplay() {
        vPortFree(this->disp_buf);
    }
};

void RunReadcpp() {

    auto curuidisp = new ReadDisplay(LCD_PIX_W, LCD_PIX_H, ll_disp_put_area);
    curuidisp->draw_printf(0, 0, 16, 0, 255, "Hello 中!");
    uint32_t key;
    uint32_t keyVal = 0;

    uint32_t delay = 0;
    uint32_t delayRel = 0;

    uint32_t cnt = 0;
    for (;;) {
        key = ll_vm_check_key();
        uint32_t press = key >> 16;
        uint32_t keyVal = key & 0xFFFF;

        if (press) {
            delay++;
            delayRel = 0;

            if (delay == 1) {
                bool continueto = curuidisp->keyMsg(keyVal, ReadDisplay::KEY_TRIG);
                if (!continueto) {
                    break;
                }
            }
            if (delay > 5) {
                if (delay % 5 == 0) {
                    bool continueto = curuidisp->keyMsg(keyVal, ReadDisplay::KEY_LONG_PRESS);
                    if (!continueto) {
                        break;
                    }

                    // main_win->winKeyMessage(keyVal, KEY_LONG_PRESS);
                    // printf("Long Press:%d\n", keyVal);
                }
            }

        } else {

            delay = 0;
            delayRel++;
            if (delayRel == 1) {
                bool continueto = curuidisp->keyMsg(keyVal, ReadDisplay::KEY_RELEASE);
                if (!continueto) {
                    break;
                }
                // main_win->winKeyMessage(keyVal, KEY_RELEASE);
                // printf("Rel:%d\n", keyVal);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
        cnt++;
        if (cnt % 45 == 0) {
        }
    }
    delete curuidisp;
}

#ifdef __cplusplus
}
#endif