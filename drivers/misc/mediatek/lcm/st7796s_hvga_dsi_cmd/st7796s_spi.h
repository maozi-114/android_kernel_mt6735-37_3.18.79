#ifndef ST7796S_SPI_H
#define ST7796S_SPI_H
void st7796s_spi_select(void);
void st7796s_spi_lock(void);
void st7796s_spi_unlock(void);
void st7796s_spi_prepare(void);
void st7796s_spi_reset(unsigned int value);
int st7796s_spi_write(unsigned int cmd, unsigned int count, const unsigned char *data);
void st7796s_spi_pause(void);
void st7796s_spi_enable(void);
void st7796s_panel_init_locked(void);
#ifdef CONFIG_MTK_YUXIN_SPI_COMPOSITION
void st7796s_spi_submit_rgb888(const void *frame, unsigned int pitch);
void st7796s_spi_submit_x8888(const void *frame, unsigned int pitch, unsigned int red);
#endif
#endif
