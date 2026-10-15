#ifndef COMPANION_H
#define COMPANION_H

#include <stdint.h>
#include <string.h>
#include <initializer_list>

// Persisted IDs: keep Pip at zero for upgrades from Ambient Fish; new pets go at the end.
enum Companion { COMPANION_FISH, COMPANION_GHOST, COMPANION_AXOLOTL, COMPANION_DINO, COMPANION_TURTLE, COMPANION_OCTOPUS, COMPANION_RED_PANDA, COMPANION_PUMPKIN,
                 COMPANION_CAT, COMPANION_FROG, COMPANION_PENGUIN, COMPANION_OWL, COMPANION_LION,
                 COMPANION_UNICORN, COMPANION_CRAB, COMPANION_COUNT };
inline bool companion_valid(int id) { return id >= 0 && id < COMPANION_COUNT; }
inline int companion_or_default(int id) { return companion_valid(id) ? id : COMPANION_FISH; }

enum CompanionStyle { COMPANION_TEXT, COMPANION_ICON, COMPANION_ONLY, COMPANION_STYLE_COUNT };
enum CompanionRange { COMPANION_IN_RANGE, COMPANION_LOW, COMPANION_HIGH };
inline bool companion_style_valid(int style) { return style >= 0 && style < COMPANION_STYLE_COUNT; }
inline int companion_style_or_default(int style) { return companion_style_valid(style) ? style : COMPANION_TEXT; }

inline uint32_t companion_color(char key) {
    switch (key) {
        case 'a': return 0xf06460; // low: red
        case 'r': return 0xf06460; // high: red
        case 'i': return 0x94e6a1; // in range: green
        case 'u': return 0x263b32; // unlit range marker
        case 'W': return 0xd1fff1;
        case 'M': return 0x86dfd1;
        case 'S': return 0x4b9d9d;
        case 'E': return 0x0b1725;
        case 'O': return 0xff9d48;
        case 'Y': return 0xffd47c;
        case 'R': return 0xe57439;
        case 'P': return 0xffc8d9;
        case 'G': return 0xec779e;
        case 'L': return 0x94e6a1;
        case 'D': return 0x50ad7c;
        case 'H': return 0xff83ac;
        case 'C': return 0xfff1d7; // cream cheeks
        case 'V': return 0xc997ff; // octopus
        case 'T': return 0x8f5c47; // red panda feet
        case 'B': return 0x74bfdc;
        case 'F': return 0x5fd35f; // frog
        case 'J': return 0xe4f59a; // frog belly
        case 'N': return 0x3d62d6; // penguin (navy: black LEDs are off)
        case 'U': return 0xb8743f; // owl
        case 'Q': return 0xf3d9a8; // owl face
        case 'A': return 0xffd98a; // lion face
        case 'K': return 0xd9822b; // lion mane
        case 'X': return 0xffe36b; // sparkle, unicorn horn
        case 'Z': return 0x8fb8ff; // sleepy "z"
        case 'c': return 0xf2554a; // crab
        case 'k': return 0xb8322b; // crab legs
        default: return 0;
    }
}

// Only status pixels use configured colors; character artwork stays unchanged.
inline uint32_t companion_resolve_color(char key, bool use_glucose_colors,
                                        uint32_t low, uint32_t in_range, uint32_t high) {
    if (use_glucose_colors) {
        if (key == 'a') return low;
        if (key == 'i') return in_range;
        if (key == 'r') return high;
    }
    return companion_color(key);
}

// Toffee, Kiko, Domino, Hoot, Leo, Stella and Kobi (ids 8-14). Draws into the
// 14-column pet area; companion_frame() centres the result. Shapes keep the same
// outline within a pose, so the per-frame centring doesn't make them jump sideways.
inline void companion_friend(int id, uint32_t ms, int phase, bool sleepy, bool happy, int range,
                             char (&pet)[8][14]) {
    auto put = [&](int x, int y, char c) { if (x >= 0 && x < 14 && y >= 0 && y < 8) pet[y][x] = c; };
    auto load = [&](const char* const* rows) {
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 14 && rows[y][x]; ++x) pet[y][x] = rows[y][x];
    };
    auto zzz = [&](int x, int y) { int step = (ms / 900) % 3; put(x + step, y - step, 'Z'); };
    const bool low = range == COMPANION_LOW, high = range == COMPANION_HIGH;

    if (id == COMPANION_CAT) {
        static const char* const cat[] = {"..O.....O.....","..OO...OO.....","..OOOROOO.....","..OEOOOEO.....",".WOOOHOOOW....","...OOOOO...O..","..OOCCCOO..O..","..OCC.CCOOO..."};
        static const char* const cat_low[] = {"..............",".OO.....OO....","..OOOROOO.....","..OOOOOOO.....",".WOEOHOEOW....","..OOOOOOO.....","..OCCCCCO.....","..OC.C.COOOOO."};
        if (low) {                                        // ears flat, tail on the floor
            load(cat_low);
            if (phase) { put(12,7,'.'); put(12,6,'O'); }
            return;
        }
        load(cat);
        if (high) {                                       // wide eyes, tail up and puffed
            put(3,4,'E'); put(7,4,'E'); put(5,5,'E');
            for (int y = 2; y < 5; ++y) put(11,y,'O');
            put(12,2,'O'); put(12,3,'O');
            if (phase) { put(12,2,'.'); put(12,1,'O'); put(11,1,'O'); put(3,7,'.'); put(4,7,'.'); put(2,6,'C'); put(3,6,'C'); }
            return;
        }
        if (phase) { put(11,5,'.'); put(12,4,'O'); }      // tail swish
        if (sleepy) { put(3,3,'R'); put(7,3,'R'); zzz(11,2); }
        if (happy) { put(9,4,'O'); put(9,5,'O'); put(phase ? 10 : 9, 3, 'C'); }
    } else if (id == COMPANION_FROG) {
        static const char* const frog[] = {".WWW.....WWW..",".WEWFFFFFWEW..","..FFFFFFFFF...",".FGFFFFFFFGF..",".FFEFFFFFEFF..","..FFEEEEEFF...","..FFJJJJJFF...",".FF.F...F.FF.."};
        load(frog);
        auto closed = [&](bool left, bool right) {
            for (int x = 0; x < 3; ++x) {
                if (left) { put(1 + x, 0, 'F'); put(1 + x, 1, 'D'); }
                if (right) { put(9 + x, 0, 'F'); put(9 + x, 1, 'D'); }
            }
        };
        if (low) {                                        // heavy lids, frown
            for (int x : {1, 2, 3, 9, 10, 11}) put(x,0,'F');
            put(2,1,'E'); put(10,1,'E'); put(3,4,'F'); put(9,4,'F'); put(3,6,'E'); put(9,6,'E');
            if (phase) closed(true, true);
        } else if (high) {                                // looking up, mouth open, cheering
            put(2,1,'W'); put(10,1,'W'); put(2,0,'E'); put(10,0,'E');
            put(4,6,'E'); put(5,6,'H'); put(6,6,'H'); put(7,6,'H'); put(8,6,'E');
            if (phase) { put(0,3,'F'); put(0,2,'F'); put(12,3,'F'); put(12,2,'F'); }
            else { put(0,4,'F'); put(12,4,'F'); }
        } else if (sleepy) {
            closed(true, true); zzz(11,3);
        } else if (happy) {                               // wink and wave
            closed(false, true); put(12,4,'F'); put(phase ? 13 : 12, 3, 'F');
        } else if (phase) {                               // looks around
            put(2,1,'W'); put(3,1,'E'); put(10,1,'W'); put(11,1,'E');
        }
    } else if (id == COMPANION_PENGUIN) {
        static const char* const penguin[] = {".....NNNN.....","....NNNNNN....","....NEWWEN....","....NWOOWN....","...NNWWWWNN...","...N.WWWW.N...","....NWWWWN....",".....OO.OO...."};
        static const char* const penguin_low[] = {"..............",".....NNNN.....","....NNNNNN....","....NWWWWN....","....NEOOEN....","...NNWWWWNN...","...NNWWWWNN...",".....OO.OO...."};
        if (low) {                                        // head bowed, flippers down
            load(penguin_low);
            if (phase) { put(5,4,'W'); put(8,4,'W'); }
            return;
        }
        load(penguin);
        if (high) {                                       // beak open, flapping
            put(6,4,'O'); put(7,4,'O'); put(3,5,'.'); put(10,5,'.');
            if (phase) { put(3,4,'.'); put(10,4,'.'); put(3,3,'N'); put(2,2,'N'); put(10,3,'N'); put(11,2,'N'); }
            else { put(2,4,'N'); put(11,4,'N'); }
        } else if (sleepy) {
            put(5,2,'W'); put(8,2,'W'); put(5,3,'E'); put(8,3,'E'); zzz(11,2);
        } else if (happy) {                               // flipper wave
            put(10,5,'.'); put(11, phase ? 3 : 4, 'N'); put(10,4,'N');
        } else if (phase) {                               // waddles from foot to foot
            put(5,7,'.'); put(9,7,'.');
        }
    } else if (id == COMPANION_OWL) {
        static const char* const owl[] = {"..UU.....UU...","...UUUUUUU....","..UQQQUQQQU...","..UQEQUQEQU...","..UQQQYQQQU...","..UUUUYUUUU...","...UQUQUQU....","....YY.YY....."};
        load(owl);
        if (low) {                                        // tufts down, heavy lids
            put(2,0,'.'); put(3,0,'.'); put(9,0,'.'); put(10,0,'.'); put(2,1,'U'); put(10,1,'U');
            for (int x : {3, 4, 5, 7, 8, 9}) put(x,2,'U');
            put(4,3,'Q'); put(8,3,'Q'); put(4,4,'E'); put(8,4,'E');
            if (phase) { put(1,5,'U'); put(11,5,'U'); }
        } else if (high) {                                // tufts up, "hoo!", wings flap
            put(3,0,'U'); put(9,0,'U'); put(6,5,'E');
            int y = phase ? 2 : 4, tip = phase ? 1 : 5;
            put(1,y,'U'); put(0,tip,'U'); put(11,y,'U'); put(12,tip,'U');
        } else if (sleepy) {
            for (int x : {3, 4, 5, 7, 8, 9}) put(x,3,'E');
            zzz(11,2);
        } else if (happy) {                               // wink and wing wave
            for (int x : {7, 8, 9}) put(x,3,'E');
            put(11,4,'U'); put(12, phase ? 2 : 3, 'U');
        } else if (phase) {                               // looks around
            put(4,3,'Q'); put(8,3,'Q'); put(3,3,'E'); put(7,3,'E');
        }
    } else if (id == COMPANION_LION) {
        static const char* const lion[] = {"...KKKKKK.....","..KKAKKAKK....",".KKAAAAAAKK...",".KAAEAAEAAK...","KKAAATTAAAKK..",".KAACCCCAAK...","..KAACCAAK....","...KKKKKK....."};
        load(lion);
        if (low) {                                        // ears flat, droopy eyes, frown
            put(4,1,'K'); put(7,1,'K'); put(4,3,'T'); put(7,3,'T'); put(5,6,'E'); put(6,6,'E');
            put(0,4,'.'); put(11,4,'.');
            if (phase) { put(1,6,'K'); put(10,6,'K'); }
        } else if (high) {                                // roar, mane bristles
            put(5,5,'E'); put(6,5,'E'); put(5,6,'H'); put(6,6,'H');
            static const int8_t spikes[2][6][2] = {{{1,0},{10,0},{0,6},{11,6},{0,2},{11,2}},
                                                   {{0,1},{11,1},{0,7},{11,7},{5,0},{6,0}}};
            for (const auto& s : spikes[phase]) put(s[0], s[1], 'K');
        } else if (sleepy) {
            put(4,3,'T'); put(7,3,'T'); zzz(11,3);
        } else if (happy) {                               // happy squint, tongue, ear wiggle
            put(4,3,'T'); put(7,3,'T'); put(5,6,'H'); put(6,6,'H');
            if (phase) { put(4,1,'K'); put(7,1,'K'); }
        } else if (phase) {                               // mane breathes
            put(0,4,'.'); put(11,4,'.'); put(0,3,'K'); put(11,3,'K');
        }
    } else if (id == COMPANION_UNICORN) {
        static const char* const unicorn[] = {"......W....X..","......WW..Y...",".....WWWWX....",".....WWEWWW...",".....WWWGWWWP.",".....WWWWWWP..",".....WWW......","....WWWW......"};
        static const char rainbow[] = "HOYLBV";
        static const int8_t mane[12][2] = {{5,1},{4,2},{4,3},{3,3},{4,4},{3,4},{4,5},{3,5},{4,6},{3,6},{3,7},{2,7}};
        load(unicorn);
        // Rainbow mane down the back of the neck; the colours flow with the phase.
        int flow = low ? 0 : phase + (happy ? 2 * ((ms / 220) % 3) : 0);
        auto paint = [&](int x, int y) { put(x, y, rainbow[(y + flow) % 6]); };
        for (const auto& p : mane) paint(p[0], p[1]);
        if (low) {                                        // head hangs, mane limp, eye down
            char tmp[8][14]; memcpy(tmp, pet, sizeof(tmp)); memset(pet, '.', sizeof(tmp));
            for (int y = 0; y < 7; ++y) for (int x = 0; x < 14; ++x) pet[y + 1][x] = tmp[y][x];
            put(7,4,'W'); put(7,5,'E');
            for (int y = 5; y < 8; ++y) put(2, y, rainbow[y % 6]);
        } else if (high) {                                // mane flies out
            static const int8_t extra[2][4][2] = {{{2,4},{2,5},{1,6},{2,6}}, {{2,3},{1,4},{2,4},{1,5}}};
            for (const auto& p : extra[phase]) paint(p[0], p[1]);
            put(7,2,'E');
        } else if (sleepy) {
            zzz(11,3);                                    // eyes stay open
        }                                                 // hello: the mane flows faster
    } else if (id == COMPANION_CRAB) {
        static const char* const crab[] = {".c.c.W..W.c.c.",".ccc.k..k.ccc.","..c..k..k..c..","..cccccccccc..","..cccccccccc..","..cGcEccEcGc..","..kcccEEccck..",".k.k.k..k.k.k."};
        static const char* const crab_low[] = {"..............",".....W..W.....",".....E..E.....","...cccccccc...",".cccccccccccc.",".ccccGcEEcGcc.","..kccEccEcck..",".k.k.k..k.k.k."};
        auto claws = [&](bool open_left, bool open_right) { put(2,0, open_left ? '.' : 'c'); put(11,0, open_right ? '.' : 'c'); };
        auto legs_step = [&]() {
            for (int x : {1, 3, 5, 6, 7, 8, 10, 12}) put(x,7,'.');
            for (int x : {2, 4, 9, 11}) put(x,7,'k');
        };
        if (low) {                                        // eyes sink, claws rest on the ground
            load(crab_low);
            if (phase) { put(5,1,'c'); put(8,1,'c'); put(5,2,'k'); put(8,2,'k'); }
            return;
        }
        load(crab);
        if (high) {                                       // snap snap, bubbles, scuttling
            put(6,5,'E'); put(7,5,'E'); put(6,6,'H'); put(7,6,'H');
            claws(!phase, !phase);
            if (phase) { put(6,1,'B'); put(7,0,'B'); legs_step(); }
            else { put(7,2,'B'); put(6,0,'B'); }
        } else if (sleepy) {                              // eyes pulled in
            claws(false, false);
            put(5,0,'.'); put(8,0,'.'); put(5,1,'c'); put(8,1,'c');
            int step = (ms / 900) % 2; put(6 + step, 1 - step, 'Z');
        } else if (happy) {                               // wink, claps its pincers
            put(8,0,'k'); claws(phase, phase);
            if (phase) legs_step();
        } else {                                          // clicks its pincers, shuffles
            claws(!phase, phase);
            if (phase) legs_step();
        }
    }
}

// Pure renderer shared by firmware and the parity-tested web preview. Draw the
// pet in a local 14-column area, then center its actual silhouette in its region.
inline void companion_frame(int id, uint32_t ms, bool sleepy, bool happy, char (&pixels)[8][32],
                            int style = COMPANION_TEXT, int range = COMPANION_IN_RANGE) {
    static const char* const fish[] = {".....YY......","....OOOOO..Y.","..YOOOOOOOYY.",".OOOEOOOOOYYY","..OOOOOOOOYY.","...RRRORR..Y.","......Y......"};
    static const char* const ghost[] = {"...WWW...","..WWWWW..",".WWWWWWW.",".WEWWEWW.",".WWWWWWW.",".MWWEWWM.",".MM.M.MM."};
    static const char* const short_ghost[] = {"...WWW...","..WWWWW..",".WEWWEWW.",".WWWWWWW.",".MWWEWWM.",".MM.M.MM."};
    static const char* const axolotl[] = {".G.......G.","..GPPPPPG..","G.PPPPPPP.G",".GPEPPPEPG.","G.PGPEPGP.G","...PPPPP...","..P.....P.."};
    static const char* const dino[3][8] = {
        {"............","......LLLLL.",".....LLLELLL",".....LLLLLLL","L...DLLLL...","LL.DLLLLLL..",".LLLLLYL....","...LL.LL...."},
        {"............","............","......LLLLL.",".....LLLELLL","L...DLLLLLLL","LLLDLLLLL...","..LLLYLLLL..","...LL.LL...."},
        {"......LLLLL.",".....LLLELLL",".....LLLLLLL","....DLLLL...","L..DLLLLLL..","LL.LLLYL....",".LLLLLLL....","....L..L...."}
    };
    static const char* const newcomers[3][3][8] = {
        {
            {"..............","....LLLL......","...LDLLDL.....","..LDDLDDDL.LL.","..LDLLLDLL.LEL",".LLLLLLLLLLLL.","...L....L.....","..LL...LL....."},
            {"..............","..............","....LLLL......","...LDLLDL.....","..LDDLDDDL....",".LLLLLLLLLLEL.","...L....L.....","..LL...LL....."},
            {"..............","....LLLL..LL..","...LDLLDL.LEL.","..LDDLDDDLLLL.","..LDLLLDLL.L..",".LLLLLLLLLLL..","...L....L.....","...LL...LL...."}
        },
        {
            {"....VVVV......","...VVVVVV.....","..VVVVVVVV....","..VCEVVCEV....","..VVVVVVVV....","...VVVVVV.....","..VV.VV.VV....",".VV..VV..VV..."},
            {"..............","....VVVV......","...VVVVVV.....","..VVVVVVVV....","..VCEVVCEV....","..VVVVVVVV....","...VVVVVV.....","..VV.VV.VV...."},
            {"....VVVV......","...VVVVVV.....","..VVVVVVVV....","..VCEVVCEV....","V.VVVVVVVV.V..","VV.VVVVVV.VV..","..VV.VV.VV....",".VV..VV..VV..."}
        },
        {
            {".O...O........",".OCCCO........","OOOOOOO.......","OCECECO....OO.","OCCYCCO...OYY.",".OOOOO...YYOO.",".TTTTT.OOYY...",".TT.TT.OO....."},
            {"..............",".O...O........",".OCCCO........","OOOOOOO.......","OCECECO.......","OCCYCCO....OO.",".OOOOO.OOYYOO.",".TT.TT.YYOO..."},
            {".O...O.....OO.",".OCCCO....OYY.","OOOOOOO...YYO.","OCECECO...OO..","OCCYCCO..YY...","OOOOOOO..OO...",".TTTTT.OO.....",".TT.TT.OO....."}
        }
    };
    // Patch the pumpkin: body only; stem, leaf, face and vine arms are drawn per pose.
    static const char* const pumpkin[] = {"...OOOROOO","..OROOOOORO",".OOROOOOOROO",".OOROOOOOROO",".OOROOOOOROO","..OROOOOORO","...OOOOOOO"};
    static const char* const squashed_pumpkin[] = {"...OOOROOO","..OROOOOORO",".OOROOOOOROO",".OOROOOOOROO",".OOROOOOOROO","..OOOOOOOOO"};
    id = companion_or_default(id);
    style = companion_style_or_default(style);
    if (range < COMPANION_IN_RANGE || range > COMPANION_HIGH) range = COMPANION_IN_RANGE;
    // An out-of-range pose stays awake and recognizable during rest hours.
    sleepy = sleepy && range == COMPANION_IN_RANGE && !happy;
    happy = happy && range == COMPANION_IN_RANGE;
    char pet[8][14]; memset(pet, '.', sizeof(pet)); memset(pixels, '.', sizeof(pixels));
    auto put = [&](int x, int y, char c) { if (x >= 0 && x < 14 && y >= 0 && y < 8) pet[y][x] = c; };
    auto sprite = [&](const char* const* rows, int count, int sx, int sy) {
        for (int y = 0; y < count; ++y) for (int x = 0; rows[y][x]; ++x)
            if (rows[y][x] != '.') put(sx + x, sy + y, rows[y][x]);
    };
    int phase = sleepy ? 0 : (ms / (happy ? 220 : range == COMPANION_LOW ? 1400 : range == COMPANION_HIGH ? 450 : 650)) % 2;
    if (id == COMPANION_FISH) {
        int sy = sleepy || range == COMPANION_LOW ? 1 : range == COMPANION_HIGH ? 0 : (ms / 1600) % 2;
        sprite(fish, 7, 0, sy);
        if (phase) { put(12, sy+3, '.'); put(11, sy, 'Y'); put(11, sy+6, 'Y'); }
        if (sleepy) { put(4, sy+3, 'O'); put(4, sy+4, 'E'); }
    } else if (id == COMPANION_GHOST) {
        bool full = range == COMPANION_IN_RANGE;
        int sy = sleepy ? 1 : range == COMPANION_LOW ? 2 : range == COMPANION_HIGH ? 0 : (ms / 1400) % 2;
        int height = full ? 7 : 6;
        sprite(full ? ghost : short_ghost, height, 3, sy);
        for (int x=1; x<8; ++x) put(3+x, sy+height-1, (x+phase)%3 == 0 ? '.' : 'M');
        if (range == COMPANION_HIGH || happy) {
            put(3, sy+3, 'W'); put(11, sy+3, 'W'); put(2, sy+(phase?2:3), 'W'); put(12, sy+(phase?2:3), 'W');
        }
        if (sleepy) for (int x : {5, 8}) { put(x, sy+3, 'W'); put(x, sy+4, 'E'); }
    } else if (id == COMPANION_AXOLOTL) {
        int sy = sleepy || range == COMPANION_LOW ? 1 : 0;
        sprite(axolotl, 7, 2, sy);
        if (range == COMPANION_LOW) {
            static const int tucked[][2] = {{1,0},{9,0},{0,2},{10,2},{0,4},{10,4}};
            for (const auto& p : tucked) put(2+p[0], sy+p[1], '.');
            if (phase) { put(3, sy+3, '.'); put(11, sy+3, '.'); put(4, sy+2, 'G'); put(10, sy+2, 'G'); }
        } else if (range == COMPANION_HIGH || happy) {
            put(2,0,'G'); put(12,0,'G'); put(1,phase?1:3,'G'); put(13,phase?1:3,'G');
            if (phase) { put(3,0,'.'); put(11,0,'.'); }
        } else if (phase) { put(3,0,'.'); put(11,0,'.'); put(2,1,'G'); put(12,1,'G'); }
        if (sleepy) for (int x : {5, 9}) { put(x, sy+3, 'P'); put(x, sy+4, 'E'); }
    } else if (id == COMPANION_DINO) {
        sprite(dino[range], 8, 1, 0);
        if (phase) {
            if (range == COMPANION_LOW) { put(1,4,'.'); put(1,5,'L'); put(2,5,'.'); put(2,6,'L'); }
            else if (range == COMPANION_IN_RANGE) { put(4,7,'.'); put(8,7,'.'); put(6,7,'D'); put(9,7,'D'); }
            else { put(5,7,'.'); put(8,7,'.'); put(6,7,'L'); put(9,7,'L'); }
        }
        if (sleepy) { put(9,2,'L'); put(9,3,'E'); }
    } else if (id == COMPANION_PUMPKIN) {
        if (range == COMPANION_LOW) {
            // Squashed and sitting low, leaf drooping, looking down with a frown.
            sprite(squashed_pumpkin, 6, 0, 2);
            put(6,1,'D'); put(phase ? 5 : 7, 1, 'L');
            put(4,4,'E'); put(8,4,'E');
            put(6,5,'E'); put(5,6,'E'); put(7,6,'E');
        } else {
            sprite(pumpkin, 7, 0, 1);
            put(6,0,'D'); put(phase ? 8 : 7, 0, 'L');      // the leaf flutters
            for (int x : {4, 8}) {
                if (sleepy) put(x,3,'E');                    // eyes closed
                else { put(x,2,'E'); put(x,3,'E'); }
            }
            put(3,4,'G'); put(9,4,'G');                      // cheeks
            if (range == COMPANION_HIGH) {
                put(6,4,'E'); put(6,5,'E');                  // open mouth
                for (int x : {0, 12}) {                      // vine arms flap
                    put(x,2,'L'); put(x, phase ? 1 : 3, 'L');
                }
            } else {
                put(5,5,'E'); put(7,5,'E'); put(6,6,'E');    // smile
                if (happy) {                                 // waves hello with one vine
                    put(0,5,'L'); put(0,6,'L'); put(12,2,'L');
                    if (phase) put(12,1,'L'); else put(13,1,'L');
                }
            }
        }
    } else if (id >= COMPANION_CAT) {
        companion_friend(id, ms, phase, sleepy, happy, range, pet);
    } else {
        sprite(newcomers[id - COMPANION_TURTLE][range], 8, 0, 0);
        if (id == COMPANION_TURTLE) {
            if (phase) { put(2,7,'.'); put(7,7,'.'); put(4,7,'L'); put(9,7,'L'); }
            if (sleepy) { put(12,4,'L'); put(12,5,'E'); }
            if (happy) { put(10,6,'L'); put(11,phase?5:6,'L'); }
        } else if (id == COMPANION_OCTOPUS) {
            if (phase) {
                put(1,7,'.'); put(10,7,'.'); put(3,7,'V'); put(8,7,'V');
                if (range == COMPANION_LOW) { put(2,7,'.'); put(9,7,'.'); }
            }
            if (sleepy) for (int x : {4,8}) { put(x,3,'V'); put(x,4,'E'); }
            if (happy) { put(0,4,'V'); put(11,4,'V'); put(1,phase?3:5,'V'); put(10,phase?3:5,'V'); }
        } else {
            if (phase) { put(7,7,'.'); put(8,7,'.'); put(9,7,'O'); put(10,6,'Y'); }
            if (sleepy) for (int x : {2,4}) { put(x,3,'C'); put(x,4,'E'); }
            if (happy) { put(0,phase?4:5,'O'); put(6,phase?4:5,'O'); }
        }
    }
    int left=14, right=-1;
    for (int y=0; y<8; ++y) for (int x=0; x<14; ++x) if (pet[y][x] != '.') {
        if (x<left) left=x; if (x>right) right=x;
    }
    int offset = ((style == COMPANION_ONLY ? 32 : 14) - (right-left+1))/2 - left;
    for (int y=0; y<8; ++y) for (int x=0; x<14; ++x)
        if (pet[y][x] != '.') pixels[y][x+offset]=pet[y][x];
    char color = range == COMPANION_LOW ? 'a' : range == COMPANION_HIGH ? 'r' : 'i';
    if (style == COMPANION_TEXT) {
        static const char letters[] = "LOWKHIG";
        static const char* const font[7][5] = {
            {"100","100","100","100","111"}, {"111","101","101","101","111"},
            {"101","101","101","111","101"}, {"101","101","110","101","101"},
            {"101","101","111","101","101"}, {"111","010","010","010","111"},
            {"111","100","101","101","111"}
        };
        const char* word = range == COMPANION_LOW ? "LOW" : range == COMPANION_HIGH ? "HIGH" : "OK";
        int start = 15 + (17 - (strlen(word)*4-1))/2;
        for (int i=0; word[i]; ++i) {
            int glyph = strchr(letters, word[i])-letters;
            for (int y=0; y<5; ++y) for (int x=0; x<3; ++x)
                if (font[glyph][y][x]=='1') pixels[y+2][start+i*4+x]=color;
        }
    } else if (style == COMPANION_ICON) {
        int lit = range == COMPANION_LOW ? 6 : range == COMPANION_HIGH ? 0 : 3;
        for (int y : {0,3,6}) for (int x=23; x<27; ++x) {
            pixels[y][x] = y==lit ? color : 'u';
            if (y==lit) pixels[y+1][x]=color;
        }
    }
}

#endif
