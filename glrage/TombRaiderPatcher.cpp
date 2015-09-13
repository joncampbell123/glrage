#include "TombRaiderPatcher.hpp"
#include "TombRaiderHooks.hpp"

#include "StringUtils.hpp"
#include "Logger.hpp"

namespace glrage {

TombRaiderPatcher::TombRaiderPatcher() :
    m_config("Tomb Raider")
{
}

bool TombRaiderPatcher::applicable(const std::string& fileName) {
    if (fileName == "tombati.exe") {
        m_ub = false;
        return true;
    }

    if (fileName == "tombub.exe") {
        m_ub = true;
        return true;
    }

    return false;
}

void TombRaiderPatcher::apply() {
    // mandatory crash patches
    applyCrashPatches();

    // optional patches
    applyGraphicPatches();
    applySoundPatches();
    applyLogicPatches();
}

void TombRaiderPatcher::applyCrashPatches() {
    // Tomb Raider ATI patch fails on later Windows versions because of a missing
    // return statement in a function.
    // In Windows 95, it is compensated by OutputDebugString's nonzero eax value,
    // while in later Windows versions, OutputDebugString returns zero.
    // Unfinished Business fails even on Windows 95, because it does not call
    // OutputDebugString at all.
    // This 'fix' injects "xor eax,eax; inc eax" into a function calling 
    // OutputDebugString, called from 'bad' function.
    // The 'bad' function can not be fixed directly because of lack of room
    // (not enough NOPs after ret).
    patch(m_ub ? 0x429ED0 : 0x42A2F6, "C3 90 90 90", "31 C0 40 C3");

    // Tihocan Centaurs and the Giant Atlantean crash the game when they explode
    // on death, because EAX is 0 in these cases.
    // This patch disables the bugged part of the routine, which doesn't appear
    // to affect visuals or sounds.
    // I guess it influences the damage, since the parts now deal a lot more
    // damage to Lara, but that's still better than no explosions or even a crash.
    patch(m_ub ? 0x43C288 : 0x43C938, "F6 C3 1C 74", "90 90 90 EB");
}

void TombRaiderPatcher::applyGraphicPatches() {
    // The ATI version of Tomb Raider converts vertex colors to half of the original
    // brightness, which results in a dim look and turns some areas in dark levels
    // almost pitch black. This patch boosts the brightness back to normal levels.
    if (m_config.getBool("patch_brightness", true)) {
        float brightness = m_config.getFloat("patch_brightness_value", 1.0f);
        float divisor = (1.0f / brightness) * 1024;
        float multi = 0.0625f * brightness;

        std::vector<uint8_t> tmp;
        appendBytes(divisor, tmp);

        patch(0x451034, "00 00 00 45", tmp);

        tmp.clear();
        appendBytes(multi, tmp);

        patch(0x45103C, "DB F6 FE 3C", tmp);
    }

    // This patch allows the customization of the water color, which is rather
    // ugly on default.
    if (m_config.getBool("patch_watercolor", true)) {
        float filterRed = m_config.getFloat("patch_watercolor_filter_red", 0.3f);
        float filterGreen = m_config.getFloat("patch_watercolor_filter_green", 1.0f);

        std::vector<uint8_t> tmp;
        appendBytes(filterRed, tmp);
        appendBytes(filterGreen, tmp);

        patch(0x451028, "9A 99 19 3F 33 33 33 3F", tmp);
    }

    // This patch replaces 800x600 with a custom resolution for widescreen
    // support and to reduce vertex artifacts due to subpixel inaccuracy,
    if (m_config.getBool("patch_resolution", true)) {
        int32_t width = m_config.getInt("patch_resolution_width", -1);
        int32_t height = m_config.getInt("patch_resolution_height", -1);

        if (width <= 0) {
            width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        }

        if (height <= 0) {
            height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        }

        std::vector<uint8_t> tmp;

        // update display mode and viewport parameters
        appendBytes(width, tmp);
        patch(m_ub ? 0x407CAA : 0x407C9D, "20 03 00 00", tmp);
        tmp.clear();

        appendBytes(height, tmp);
        patch(m_ub ? 0x407CB4 : 0x407CA7, "58 02 00 00", tmp);
        tmp.clear();

        appendBytes(static_cast<float_t>(width - 1), tmp);
        patch(m_ub ? 0x407CBE : 0x407CB1, "00 C0 47 44", tmp);
        tmp.clear();

        appendBytes(static_cast<float_t>(height - 1), tmp);
        patch(m_ub ? 0x407CC8 : 0x407CBB, "00 C0 15 44", tmp);
        tmp.clear();

        // update clipping size
        appendBytes(static_cast<int16_t>(width), tmp);
        patch(m_ub ? 0x408A64 : 0x408A57, "20 03", tmp);
        tmp.clear();

        appendBytes(static_cast<int16_t>(height), tmp);
        patch(m_ub ? 0x408A6D : 0x408A60, "58 02", tmp);
        tmp.clear();

        // set display string (needs to be static so the data won't vanish after
        // patching has finished)
        static std::string displayMode = StringUtils::format("%dx%d", 24, width, height);

        appendBytes(reinterpret_cast<int32_t>(displayMode.c_str()), tmp);
        if (m_ub) {
            patch(0x42DB5B, "40 61 45 00 ", tmp);
        } else {
            patch(0x42DF6B, "58 67 45 00", tmp);
        }
        tmp.clear();
    }

    // Not sure what exactly this value does, but setting it too low sometimes
    // produces wrong vertex positions on the far left and right side of the
    // screen, especially on high resolutions.
    // Raising it from 10 to the maximum value of 127 fixes that.
    std::string tmpExp = "0A";
    std::string tmpRep = "7F";
    patch(m_ub ? 0x4163E9 : 0x4164D9, tmpExp, tmpRep);
    patch(m_ub ? 0x41657A : 0x41666A, tmpExp, tmpRep);
    patch(m_ub ? 0x41666E : 0x41675E, tmpExp, tmpRep);
    patch(m_ub ? 0x416801 : 0x4168F1, tmpExp, tmpRep);
    patch(m_ub ? 0x4168FE : 0x4169EE, tmpExp, tmpRep);

    // This patch raises the maximum FPS from 30 to 60.
    // FIXME: disabled, since only actually works in menu while the game itself
    // just renders duplicate frames.
    //if (m_config.getBool("patch_60fps", true)) {
    //    // render on every tick instead of every other
    //    patch(m_ub ? 0x408A91 : 0x408A84, "02", "01");
    //    // disables frame skipping, which also fixes issues with the demo mode
    //    // if the frame rate isn't exactly at limit all the time
    //    patch(m_ub ? 0x408ABA : 0x408AAD, "83 E1 1F", "33 C9 90");
    //}
}

void TombRaiderPatcher::applySoundPatches() {
    // For reasons unknown, the length of a sound sample is stored in a struct
    // field as a 16 bit integer, which means that the maximum allowed length for
    // a sound sample is be 65535 bytes. If a sample is larger, which happens quite
    // often for Lara's speeches in her home, then there's an integer overflow 
    // and the length is wrapped to the modulo of 65535. This causes her speech
    // to cut off, if too long. In one case ("Ah, the main hall..."), the sample
    // is just slightly larger than 64k, which causes the game to play only the
    // first few milliseconds of silence, hence the sample appears to be missing
    // in the ATI patch.
    // This patch extracts the correct 32 bit length from the RIFF data directly,
    // which fixes this bug.
    patch(m_ub ? 0x419ED8 : 0x419FC8 ,"66 8B 7B 04", "8B 7E FC 90");

    // Pass raw pan values to the sound functions to maintain full precision.
    std::string panPatchOriginal = "C1 F8 08 05 80 00 00 00";
    std::string panPatchReplace  = "90 90 90 90 90 90 90 90";
    patch(m_ub ? 0x4385DF : 0x438C1F, panPatchOriginal, panPatchReplace);
    patch(m_ub ? 0x438631 : 0x438C71, panPatchOriginal, panPatchReplace);
    patch(m_ub ? 0x4386E0 : 0x438D20, panPatchOriginal, panPatchReplace);

    // The ATI patch lacks support for looping sounds. This patch finishes the
    // undone work and replaces the sound loop function stubs with actual
    // implementations.
    // It also replaces the subroutine for normal sounds to fix the annoying
    // panning issue.
    TombRaiderHooks::m_tombSoundInit = reinterpret_cast<TombRaiderSoundInit*>(m_ub ? 0x419DA0 : 0x419E90);
    TombRaiderHooks::m_tombSampleTable = reinterpret_cast<TombRaiderAudioSample***>(m_ub ? 0x45B314 : 0x45B954);
    TombRaiderHooks::m_tombSoundInit1 = reinterpret_cast<int32_t*>(m_ub ? 0x459CF4 : 0x45A31C);
    TombRaiderHooks::m_tombSoundInit2 = reinterpret_cast<int32_t*>(m_ub ? 0x459CF8 : 0x45A320);
    TombRaiderHooks::m_tombDecibelLut = reinterpret_cast<int32_t*>(m_ub ? 0x45E9E0 : 0x45F1E0);

    if (m_ub) {
        patchAddr(0x437B59, "E8 42 22 FE FF", &TombRaiderHooks::soundInit, 0xE8);
        patchAddr(0x4386CA, "E8 01 18 FF FF", &TombRaiderHooks::setVolume, 0xE8);
        patchAddr(0x4386EA, "E8 E1 17 FF FF", &TombRaiderHooks::setPan, 0xE8);
        patchAddr(0x4385F2, "E8 29 F2 FF FF", &TombRaiderHooks::playOneShot, 0xE8);
        patchAddr(0x438648, "E8 A3 F2 FF FF", &TombRaiderHooks::playLoop, 0xE8);
        patchAddr(0x42EAF8, "E8 F3 8D 00 00", &TombRaiderHooks::playLoop, 0xE8);
    } else {
        patchAddr(0x438129, "E8 62 1D FE FF", &TombRaiderHooks::soundInit, 0xE8);
        patchAddr(0x438D0A, "E8 21 F2 FF FF", &TombRaiderHooks::setVolume, 0xE8);
        patchAddr(0x438D2A, "E8 01 F2 FF FF", &TombRaiderHooks::setPan, 0xE8);
        patchAddr(0x438C32, "E8 D9 F1 FF FF", &TombRaiderHooks::playOneShot, 0xE8);
        patchAddr(0x438C88, "E8 33 EF FF FF", &TombRaiderHooks::playLoop, 0xE8);
        patchAddr(0x42EF35, "E8 86 8C 00 00", &TombRaiderHooks::playLoop, 0xE8);
    }

    // Very optional patch: replace ambient track "derelict" with "water", which,
    // in my very personal opinion, is more fitting for the theme of this level.
    if (!m_ub && m_config.getBool("patch_lostvalley_ambience", false)) {
        patch(0x456A1E, "39", "3A");
    }

    // Soundtrack patch. Allows both ambient and music cues to be played via MCI.
    if (!m_ub && m_config.getBool("patch_soundtrack", false)) {
        TombRaiderHooks::m_tombCDStop = reinterpret_cast<TombRaiderCDStop*>(0x437F80);
        TombRaiderHooks::m_tombCDPlay = reinterpret_cast<TombRaiderCDPlay*>(0x437FB0);
        TombRaiderHooks::m_tombTrackID = reinterpret_cast<int32_t*>(0x4534DC);
        TombRaiderHooks::m_tombTrackIDLoop = reinterpret_cast<int32_t*>(0x45B97C);

        // level music
        patchAddr(0x438D40, "66 83 3D 34 63", &TombRaiderHooks::playCDTrack, 0xE9);
        // cutscene music (copy of the sub above)
        patchAddr(0x439030, "66 83 3D 34 63", &TombRaiderHooks::playCDTrack, 0xE9);

        // fix jump in CD stop sub that normally also handles NPC voice samples
        patch(0x438E4F, "7C", "EB");

        // also pass 0 to the CD play sub when loading a level so the background
        // track can be silenced correctly
        patch(0x43639E, "74 09", "90 90");
    }
}

void TombRaiderPatcher::applyLogicPatches() {
    // This changes the first drive letter to search for the Tomb Raider CD from 'C'
    // to 'A', which allows the game to find CDs placed in the drives A: or B: in
    // systems with no floppy drives.
    patch(m_ub ? 0x41BF50 : 0x41C020, "B0 43", "B0 41");

    // This patch fixes a bug in the global key press handling, which normally
    // interrupts the demo mode and the credit sceens immediately after any key
    // has ever been pressed while the game is running.
    if (!m_ub) {
        // hook key event subroutine
        TombRaiderHooks::m_tombKeyStates = reinterpret_cast<uint8_t**>(0x45B998);
        patchAddr(0x43D904, "E8 67 A2 FF FF", &TombRaiderHooks::keyEvent, 0xE8);
    }

    // Fix infinite loop before starting the credits.
    patch(m_ub ? 0x41CC88 : 0x41CD58, "74", "EB");

    // Fix black frames in between the credit screens.
    if (m_ub) {
        patch(0x41D1F3, "D9 CC 00 00", "57 BC FE FF");
        patch(0x41D226, "A6 CC 00 00", "24 BC FE FF");
        patch(0x41D259, "73 CC 00 00", "F1 BB FE FF");
        patch(0x41D28C, "40 CC 00 00", "BE BB FE FF");
        patch(0x41D2BF, "0D CC 00 00", "8B BB FE FF");
    } else {
        patch(0x41D48F, "9D AA 01 00", "AE B9 FE FF");
        patch(0x41D4C2, "6A AA 01 00", "7B B9 FE FF");
        patch(0x41D4F5, "37 AA 01 00", "48 B9 FE FF");
        patch(0x41D528, "04 AA 01 00", "15 B9 FE FF");
    }

    // No-CD patch. Allows the game to load game files and movies from the local
    // directory instead from the CD.
    if (m_config.getBool("patch_nocd", false)) {
        // disable CD check call
        if (m_ub) {
            patch(0x41DE7F, "E8 CC E0 FF FF", "90 90 90 90 90");
        } else {
            patch(0x41E17F, "E8 9C DE FF FF", "90 90 90 90 90");
        }

        // fix format string: "%c:\%s" -> "%s"
        patch(m_ub ? 0x453730 : 0x453890, "25 63 3A 5C 25 73", "25 73 00 00 00 00");

        if (m_ub) {
            // disable drive letter argument in sprintf call
            patch(0x41BF15, "50", "90");
            patch(0x41BF35, "51", "90");
            patch(0x41BF47, "10", "0C");

            patch(0x41AEFC, "50", "90");
            patch(0x41AF0B, "51", "90");
            patch(0x41AF1D, "10", "0C");
        } else {
            // swap drive letter and path in sprintf call
            patch(0x41BFF9, "52 50", "50 52");
            patch(0x41AFE1, "52 50", "50 52");
        }
    }

    // Random fun patches, discovered from various experiments.
    
    // Crazy/creepy SFX mod. Forces a normally unused raw reading mode on all 
    // level samples. The result is hard to describe, just try it out and listen.
    // (requires sample length patch above to be disabled)
    //patch(0x437D1C, "75", "EB");

    // This forces all sounds to be played globally with full volume regardless
    // of their distance to Lara. Can be useful for sound debugging.
    //patch(0x42AAC6, "75 15", "90 90");

    // Underwater mod. Render everything as if it was beneath water. Trippy!
    //patch(0x417216, "26 94", "C6 93");
    //patch(0x416E08, "34 98", "D4 97");
}

}