#ifndef ST7796S_SPI_H
#define ST7796S_SPI_H
void st7796s_spi_select(void);
void st7796s_spi_lock(void);
void st7796s_spi_unlock(void);
void st7796s_spi_prepare(void);
void st7796s_spi_reset(unsigned int value);
void st7796s_spi_write(unsigned int cmd, unsigned int count, const unsigned char *data);
void st7796s_spi_pixels(const unsigned char *data, unsigned int count);
void st7796s_spi_pause(void);
void st7796s_spi_enable(void);
void st7796s_panel_init(void);
#endif
