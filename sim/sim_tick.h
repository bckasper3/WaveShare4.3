#pragma once
/* Simulator clock (ms). Real time in the interactive window, virtual time in --shots mode. */
#include <stdint.h>
uint32_t sim_tick_ms(void);
