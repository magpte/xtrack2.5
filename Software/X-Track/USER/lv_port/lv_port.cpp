#include "lv_port.h"
#include "Utils/TileBundleFS/TileBundleFS.h"   // 路径按你实际放置 TileBundleFS.h/.cpp 的位置调整

void lv_port_init(void)
{
    lv_port_disp_init();
    lv_port_indev_init();
    lv_port_fs_init();
    TileBundleFS_Init();   // 必须在 lv_port_fs_init() 之后——它要复用 SD 卡那条盘符的驱动
}
