#include "../Generated/Can_Cfg.h"

int main(void)
{
    return Can_GetConfiguredMessageCount() == 2U ? 0 : 1;
}
