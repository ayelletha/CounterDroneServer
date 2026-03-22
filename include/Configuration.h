#include "Generals.h"

extern LogLevel LOG_LEVEL;

inline int MAX_PKGS_AMOUNT = 1000; // value of -1 means no limit. i.e. infinite run untill user's interrupt

inline double PKGS_RATE = 100.0; // pkg per second (<1 means a songle package each moe than 1 second)