#pragma once
#include <vector>

struct ChoirPreset
{
    const char* name;
    float voices, detune, timing, vibrato, width, tone, octDown, octUp, grit, room, roomSize, choir, dry;
};

namespace openchoir
{
inline const std::vector<ChoirPreset>& factoryPresets()
{
    //                        voices det  time  vib  width tone    octD octU grit room size choir  dry
    static const std::vector<ChoirPreset> list {
        { "Gospel Choir",        12, 14,  30,   45,  85,   8000,   25,  0,   0,   35,  70,   0.0f, -6.0f  },
        { "Big Hall Choir",      16, 18,  40,   40, 100,   7000,   30, 15,   0,   55,  90,  -1.0f, -12.0f },
        { "Gang Shout",          10, 22,  60,    5,  90,  10000,   20,  0,  45,   18,  35,   0.0f, -4.0f  },
        { "Bright Octave Choir", 10, 12,  25,   50,  80,  11000,    0, 30,   0,   40,  70,   0.0f, -6.0f  },
        { "Low Monk Chant",       8, 10,  30,   20,  70,   4500,   70,  0,   0,   60,  95,   0.0f, -8.0f  },
        { "Tight Double",         3,  8,  12,   15,  60,  14000,    0,  0,   0,    8,  30,  -3.0f,  0.0f  },
    };
    return list;
}
}
