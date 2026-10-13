// =========================================================================
// TFT_eSPI User_Setup.h для проекта: ESP32-2,4TFT-WiFI-Climate-Camera v2.1.0
// Контроллер дисплея: ILI9341 (2.4" TFT, 240x320, 4-проводной SPI)
// =========================================================================

#define USER_SETUP_INFO "ILI9341_240x320_ESP32_ClimateCamera_v2.1.0"

// Выбор драйвера
#define ILI9341_DRIVER

// Разрешение дисплея
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Назначение выводов ESP32
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   19  // Выделенный пин CS для ILI9341
#define TFT_DC   25  // Data/Command (A0)
#define TFT_RST  13  // Reset (RES)
#define TFT_MISO -1  // Чтение с дисплея не используется

// Шрифты
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// Тактовая частота SPI
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  20000000
