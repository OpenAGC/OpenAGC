#include "agc_graphics.h"

int main(void)
{
    AgcGfx1013TessellationRingTable table = {{0}};
    return sizeof(table) == 128u ? 0 : 1;
}
