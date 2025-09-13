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
#include "Fs/Fatfs/ff.h"

void memtest(uint32_t testSize);
void ll_disp_put_area(uint8_t *vbuffer, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);

// 定义一个结构来表示显示行
struct DisplayLine {
    char* start;      // 行开始位置
    int length;       // 行长度
};

class ReadDisplay {
public:
    static const int KEY_TRIG = 0;
    static const int KEY_LONG_PRESS = 1;
    static const int KEY_RELEASE = 2;

private:
    uint8_t *disp_buf;
    int disp_w, disp_h;
    void (*drawf)(uint8_t *buf, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
    
    // 文本阅读器相关变量
    TCHAR *file_path;
    char *file_content;
    size_t file_size;
    int lines_per_page;
    int chars_per_line;
    int current_page;
    
    // 用于处理换行的显示行数组
    struct DisplayLine* display_lines;
    int total_display_lines;
    
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
        
        // 初始化文本阅读器变量
        this->file_path = NULL;
        this->file_content = NULL;
        this->file_size = 0;
        this->lines_per_page = (display_height - 16) / 12;  // 为页码留出空间，使用12号字体
        this->chars_per_line = (display_width - 10) / 6;    // 左右边距各5像素，使用6像素宽字体
        this->current_page = 0;
        this->display_lines = NULL;
        this->total_display_lines = 0;
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

    // 打开并加载文本文件
    bool openTextFile( TCHAR *path) {
        FIL fil;
        FRESULT res;
        
        // 保存文件路径
        size_t path_len = strlen(path) + 1;
        this->file_path = (TCHAR*)pvPortMalloc(path_len * sizeof(TCHAR));
        if (this->file_path == NULL) return false;
        strcpy(this->file_path, path);
        
        // 打开文件
        res = f_open(&fil, path, FA_READ);
        if (res != FR_OK) {
            vPortFree(this->file_path);
            this->file_path = NULL;
            return false;
        }
        
        // 获取文件大小
        this->file_size = f_size(&fil);
        if (this->file_size == 0) {
            f_close(&fil);
            return false;
        }
        
        // 分配内存并读取文件内容
        this->file_content = (char*)pvPortMalloc(this->file_size + 1);
        if (this->file_content == NULL) {
            f_close(&fil);
            vPortFree(this->file_path);
            this->file_path = NULL;
            return false;
        }
        
        UINT bytes_read;
        res = f_read(&fil, this->file_content, this->file_size, &bytes_read);
        f_close(&fil);
        
        if (res != FR_OK || bytes_read != this->file_size) {
            vPortFree(this->file_content);
            this->file_content = NULL;
            vPortFree(this->file_path);
            this->file_path = NULL;
            return false;
        }
        
        // 添加字符串结束符
        this->file_content[this->file_size] = '\0';
        
        // 处理文本内容，创建行指针数组
        this->processTextContent();
        
        return true;
    }
    
    // 处理文本内容，分割行（考虑屏幕宽度）
    void processTextContent() {
        // 释放之前的显示行数组
        if (this->display_lines) {
            vPortFree(this->display_lines);
            this->display_lines = NULL;
            this->total_display_lines = 0;
        }
        
        // 第一次遍历，计算实际需要的显示行数
        this->total_display_lines = 0;
        char* ptr = this->file_content;
        
        while (ptr < this->file_content + this->file_size) {
            // 查找下一个换行符
            char* newline = strchr(ptr, '\n');
            if (!newline) {
                newline = this->file_content + this->file_size; // 文件末尾
            }
            
            // 计算这一段文本的长度
            int line_length = newline - ptr;
            
            // 处理回车符
            if (line_length > 0 && *(newline - 1) == '\r') {
                line_length--;
            }
            
            // 根据屏幕宽度计算需要的显示行数
            int lines_needed = (line_length + this->chars_per_line - 1) / this->chars_per_line;
            if (lines_needed == 0) lines_needed = 1; // 至少需要一行（空行情况）
            
            this->total_display_lines += lines_needed;
            ptr = newline + 1;
        }
        
        // 分配显示行数组
        this->display_lines = (struct DisplayLine*)pvPortMalloc(this->total_display_lines * sizeof(struct DisplayLine));
        if (this->display_lines == NULL) {
            return;
        }
        
        // 第二次遍历，填充显示行数组
        ptr = this->file_content;
        int display_line_index = 0;
        
        while (ptr < this->file_content + this->file_size && display_line_index < this->total_display_lines) {
            // 查找下一个换行符
            char* newline = strchr(ptr, '\n');
            if (!newline) {
                newline = this->file_content + this->file_size;
            }
            
            // 计算这一段文本的长度
            int line_length = newline - ptr;
            
            // 处理回车符
            if (line_length > 0 && *(newline - 1) == '\r') {
                line_length--;
            }
            
            // 如果行长度为0（空行）
            if (line_length == 0) {
                this->display_lines[display_line_index].start = ptr;
                this->display_lines[display_line_index].length = 0;
                display_line_index++;
            } else {
                // 如果行长度小于等于每行最大字符数，直接添加
                if (line_length <= this->chars_per_line) {
                    this->display_lines[display_line_index].start = ptr;
                    this->display_lines[display_line_index].length = line_length;
                    display_line_index++;
                } else {
                    // 否则，需要分成多行显示
                    for (int i = 0; i < line_length; i += this->chars_per_line) {
                        this->display_lines[display_line_index].start = ptr + i;
                        int remaining = line_length - i;
                        this->display_lines[display_line_index].length = (remaining > this->chars_per_line) ? this->chars_per_line : remaining;
                        display_line_index++;
                    }
                }
            }
            
            // 移动到下一行
            ptr = newline + 1;
        }
        
        this->current_page = 0;
    }
    
    // 显示当前页
    void displayCurrentPage() {
        // 清空显示缓冲区
        memset(this->disp_buf, 0xff, this->disp_w * this->disp_h);
        
        int start_line = this->current_page * this->lines_per_page;
        int end_line = start_line + this->lines_per_page;
        if (end_line > this->total_display_lines) {
            end_line = this->total_display_lines;
        }
        
        // 显示文本内容
        for (int i = start_line; i < end_line; i++) {
            int y_pos = (i - start_line) * 12;
            if (y_pos + 12 <= this->disp_h - 16) {  // 为页码留出空间
                this->drawTextLine(5, y_pos, 
                                   this->display_lines[i].start, 
                                   this->display_lines[i].length);
            }
        }
        
        // 显示页码
        int total_pages = (this->total_display_lines + this->lines_per_page - 1) / this->lines_per_page;
        if (total_pages == 0) total_pages = 1;
        
        char page_info[32];
        sprintf(page_info, "%d/%d", this->current_page + 1, total_pages);
        this->drawTextLine((this->disp_w - strlen(page_info) * 6) / 2, this->disp_h - 12, page_info, strlen(page_info));
        
        // 刷新显示
        this->drawf(this->disp_buf, 0, 0, this->disp_w - 1, this->disp_h - 1);
    }
    
    // 绘制文本行
    void drawTextLine(uint32_t x0, uint32_t y0, const char *text, int length) {
        if (text == NULL || length <= 0) return;
        
        // 创建临时字符串用于显示
        char* display_text = (char*)pvPortMalloc(length + 1);
        if (display_text == NULL) return;
        
        strncpy(display_text, text, length);
        display_text[length] = '\0';
        
        // 使用draw_printf函数绘制文本，支持中文显示
        this->draw_printf(x0, y0, 12, 0, 0xFF, "%s", display_text);
        
        vPortFree(display_text);
    }
    
    // 翻页处理
    void nextPage() {
        int total_pages = (this->total_display_lines + this->lines_per_page - 1) / this->lines_per_page;
        if (this->current_page < total_pages - 1) {
            this->current_page++;
            this->displayCurrentPage();
        }
    }
    
    // 上一页
    void prevPage() {
        if (this->current_page > 0) {
            this->current_page--;
            this->displayCurrentPage();
        }
    }
    
    // 下半页
    void nextHalfPage() {
        int half_page_lines = this->lines_per_page / 2;
        int current_line = this->current_page * this->lines_per_page;
        
        // 计算新的页面位置
        int new_line = current_line + half_page_lines;
        int max_line = this->total_display_lines - 1;
        if (new_line > max_line) new_line = max_line;
        
        this->current_page = new_line / this->lines_per_page;
        this->displayCurrentPage();
    }
    
    // 上半页
    void prevHalfPage() {
        int half_page_lines = this->lines_per_page / 2;
        int current_line = this->current_page * this->lines_per_page;
        
        // 计算新的页面位置
        int new_line = current_line - half_page_lines;
        if (new_line < 0) new_line = 0;
        
        this->current_page = new_line / this->lines_per_page;
        this->displayCurrentPage();
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
            else if (key == KEY_LEFT)
                this->prevPage();  // 左键翻上一页
            else if (key == KEY_RIGHT)
                this->nextPage();  // 右键翻下一页
            else if (key == KEY_UP)
                this->prevHalfPage();  // 上键翻上半页
            else if (key == KEY_DOWN)
                this->nextHalfPage();  // 下键翻下半页
            return true;
        default:
            return true;
        }
    };

    ~ReadDisplay() {
        if (this->disp_buf) {
            vPortFree(this->disp_buf);
        }
        if (this->file_content) {
            vPortFree(this->file_content);
        }
        if (this->file_path) {
            vPortFree(this->file_path);
        }
        if (this->display_lines) {
            vPortFree(this->display_lines);
        }
    }
};

void RunReadcpp( char* file_path) {
    size_t len = strlen(file_path);
    if (len < 5) return ;  // ".txt" 需要 4 个字符，至少还需要 1 个字符的文件名
    
    // 检查最后 4 个字符是否为 ".txt"
     if(!strcmp(file_path + len - 4, ".txt") == 0)
     {
        return ;
     };

    auto curuidisp = new ReadDisplay(LCD_PIX_W, LCD_PIX_H, ll_disp_put_area);
    
    // 打开并显示文本文件
    if (curuidisp->openTextFile(file_path)) {
        curuidisp->displayCurrentPage();
    } else {
        curuidisp->draw_printf(0, 0, 12, 0, 255, "Failed to open file!");
    }
    
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