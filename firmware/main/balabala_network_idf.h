#ifndef BALABALA_NETWORK_IDF_H
#define BALABALA_NETWORK_IDF_H
#include "balabala_network.h"
/* 初始化静态控制器，不启动无线；只能由主平台任务驱动step。 */
BalabalaNetwork *balabala_network_idf_init(void);
#endif
