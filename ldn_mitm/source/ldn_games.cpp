/* SPDX-License-Identifier: GPL-2.0-only */
#include "ldn_games.hpp"
#include "debug.hpp"
#include <cinttypes>

namespace ams::mitm::ldn::games {

    namespace {

        /* Well-known local play games (base application IDs). A missing or wrong
         * entry only means that game is learned on its first use. */
        constexpr u64 BuiltIn[] = {
            0x0100152000022000, /* Mario Kart 8 Deluxe */
            0x01006A800016E000, /* Super Smash Bros. Ultimate */
            0x01003BC0000A0000, /* Splatoon 2 */
            0x0100C2500FC20000, /* Splatoon 3 */
            0x01006F8002326000, /* Animal Crossing: New Horizons */
            0x010028600EBDA000, /* Super Mario 3D World + Bowser's Fury */
            0x01006FE013472000, /* Mario Party Superstars */
            0x010036B0034E4000, /* Super Mario Party */
            0x0100B04011742000, /* Monster Hunter Rise */
            0x0100770008DD8000, /* Monster Hunter Generations Ultimate */
            0x0100B3F000BE2000, /* Pokken Tournament DX */
            0x01009B500007C000, /* ARMS */
            0x010047700D540000, /* Clubhouse Games: 51 Worldwide Classics */
            0x0100ABF008968000, /* Pokemon Sword */
            0x01008DB008C2C000, /* Pokemon Shield */
            0x0100000011D90000, /* Pokemon Brilliant Diamond */
            0x010018E011D92000, /* Pokemon Shining Pearl */
            0x01001F5010DFA000, /* Pokemon Legends: Arceus */
            0x0100A3D008C5C000, /* Pokemon Scarlet */
            0x01008F6008C5E000, /* Pokemon Violet */
            0x010003F003A34000, /* Pokemon Let's Go Pikachu */
            0x0100187003A36000, /* Pokemon Let's Go Eevee */
            0x010019401051C000, /* Mario Strikers: Battle League */
            0x0100BDE00862A000, /* Mario Tennis Aces */
            0x0100DCA0064A6000, /* Luigi's Mansion 3 */
            0x01002B00111A2000, /* Hyrule Warriors: Age of Calamity */
            0x0100F15003E64000, /* Fire Emblem Warriors */
            0x01009B90006DC000, /* Super Mario Maker 2 */
            0x010060700AC50000, /* Marvel Ultimate Alliance 3 */
            0x01001B300B9BE000, /* Diablo III */
            0x0100D71004694000, /* Minecraft */
            0x01005EE0036EC000, /* Rocket League */
            0x0100A250097F0000, /* Dragon Ball FighterZ */
            0x0100E65002BB8000, /* Stardew Valley */
            0x0100F28011892000, /* Overcooked! All You Can Eat */
            0x01009510001CA000, /* FAST RMX */
        };

        constexpr const char Path[] = "sdmc:/config/ldn_mitm/ldngames.bin";
        constexpr size_t MaxLearned = 64;

        constinit os::SdkMutex g_lock;
        constinit u64 g_learned[MaxLearned] = {};
        constinit bool g_loaded = false;

        void LoadLocked() {
            if (g_loaded) {
                return;
            }
            g_loaded = true;
            fs::FileHandle f;
            if (R_SUCCEEDED(fs::OpenFile(std::addressof(f), Path, fs::OpenMode_Read))) {
                size_t got = 0;
                static_cast<void>(fs::ReadFile(std::addressof(got), f, 0, g_learned, sizeof(g_learned)));
                fs::CloseFile(f);
            }
        }

        void SaveLocked() {
            static_cast<void>(fs::CreateDirectory("sdmc:/config"));
            static_cast<void>(fs::CreateDirectory("sdmc:/config/ldn_mitm"));
            static_cast<void>(fs::DeleteFile(Path));
            if (R_FAILED(fs::CreateFile(Path, sizeof(g_learned)))) {
                return;
            }
            fs::FileHandle f;
            if (R_SUCCEEDED(fs::OpenFile(std::addressof(f), Path, fs::OpenMode_Write))) {
                static_cast<void>(fs::WriteFile(f, 0, g_learned, sizeof(g_learned), fs::WriteOption::Flush));
                fs::CloseFile(f);
            }
        }

    }

    bool IsKnown(u64 program_id) {
        if (program_id == 0) {
            return false;
        }
        for (const u64 id : BuiltIn) {
            if (id == program_id) {
                return true;
            }
        }
        std::scoped_lock lk(g_lock);
        LoadLocked();
        for (const u64 id : g_learned) {
            if (id == program_id) {
                return true;
            }
        }
        return false;
    }

    bool Learn(u64 program_id) {
        if (program_id == 0 || IsKnown(program_id)) {
            return false;
        }
        std::scoped_lock lk(g_lock);
        size_t slot = MaxLearned - 1;           /* full: replace the oldest */
        for (size_t i = 0; i < MaxLearned; i++) {
            if (g_learned[i] == 0) { slot = i; break; }
        }
        if (slot == MaxLearned - 1 && g_learned[slot] != 0) {
            for (size_t i = 0; i + 1 < MaxLearned; i++) g_learned[i] = g_learned[i + 1];
        }
        g_learned[slot] = program_id;
        SaveLocked();
        LogFormat("games: learned %016" PRIx64, program_id);
        return true;
    }

}
