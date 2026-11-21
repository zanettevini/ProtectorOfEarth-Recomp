// save_location.cpp: the memory cards live in a per-user folder, not next to the game's files.
//
// WHAT IT IS FOR
//   PS2Recomp's runtime keeps memory card 0 in "mc0/" (and card 1 in
//   "mc1/") next to the game's ELF, so here inside game/: the player's saves
//   sit among the disc files and are lost if game/ is replaced
//   (KNOWN_ISSUES R21).
//
// WHAT IT DOES
//   Moves the cards to a per-user folder:
//     - BEN10_SAVE_DIR, if set (the cards go in BEN10_SAVE_DIR/mc0 and mc1);
//     - Windows: %APPDATA%\ProtectorOfEarth-Recomp;
//     - elsewhere: $XDG_DATA_HOME/protectorofearth-recomp, or
//       ~/.local/share/protectorofearth-recomp.
//   On first use, when a card folder does not exist in the new place but
//   exists next to the ELF, it is copied there (the old copy is left as it
//   is). If no folder can be found or created, or the copy fails, the cards
//   stay next to the ELF, as before, and the log says why.
//
// HOW IT HOOKS IN
//   A game override: PS2Runtime::loadELF sets the I/O paths from the ELF
//   (mcRoot = <elf dir>/mc0) and then applies the overrides, so this one
//   runs before the game starts and changes only mcRoot with
//   PS2Runtime::setIoPaths. The memory card stubs and the IOP's file
//   system read the paths at every call; card 1's folder is derived from
//   card 0's ("mc0" -> sibling "mc1").

#include "ps2_runtime.h"
#include "game_overrides.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace ben10::saves
{
    namespace fs = std::filesystem;

    fs::path envPath(const char *name)
    {
#ifdef _WIN32
        // Wide API: user folders may contain characters outside the ANSI code page.
        std::wstring wideName(name, name + std::char_traits<char>::length(name));
        const wchar_t *value = _wgetenv(wideName.c_str());
        return (value != nullptr && *value != L'\0') ? fs::path(value) : fs::path();
#else
        const char *value = std::getenv(name);
        return (value != nullptr && *value != '\0') ? fs::path(value) : fs::path();
#endif
    }

    fs::path userSaveFolder()
    {
        if (fs::path explicitDir = envPath("BEN10_SAVE_DIR"); !explicitDir.empty())
            return explicitDir;
#ifdef _WIN32
        if (fs::path appData = envPath("APPDATA"); !appData.empty())
            return appData / "ProtectorOfEarth-Recomp";
#else
        if (fs::path dataHome = envPath("XDG_DATA_HOME"); !dataHome.empty())
            return dataHome / "protectorofearth-recomp";
        if (fs::path home = envPath("HOME"); !home.empty())
            return home / ".local" / "share" / "protectorofearth-recomp";
#endif
        return {};
    }

    // Copies oldCard to newCard when newCard does not exist yet. False if the
    // new folder cannot be used.
    bool prepareCard(const fs::path &oldCard, const fs::path &newCard)
    {
        std::error_code ec;
        if (fs::exists(newCard, ec))
            return fs::is_directory(newCard, ec);
        if (!oldCard.empty() && fs::is_directory(oldCard, ec))
        {
            fs::copy(oldCard, newCard, fs::copy_options::recursive, ec);
            if (ec)
            {
                std::fprintf(stderr, "[saves] could not copy %s to %s: %s\n",
                             oldCard.string().c_str(), newCard.string().c_str(), ec.message().c_str());
                fs::remove_all(newCard, ec);
                return false;
            }
            std::fprintf(stderr, "[saves] copied %s to %s (the old folder is kept)\n",
                         oldCard.string().c_str(), newCard.string().c_str());
        }
        return true;
    }

    void apply(PS2Runtime &)
    {
        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
        const fs::path oldCard0 = paths.mcRoot;
        const fs::path folder = userSaveFolder();
        if (folder.empty())
        {
            std::fprintf(stderr, "[saves] no per-user folder (BEN10_SAVE_DIR, APPDATA or HOME unset); memory cards stay in %s\n",
                         oldCard0.string().c_str());
            return;
        }

        std::error_code ec;
        fs::create_directories(folder, ec);
        if (ec)
        {
            std::fprintf(stderr, "[saves] could not create %s (%s); memory cards stay in %s\n",
                         folder.string().c_str(), ec.message().c_str(), oldCard0.string().c_str());
            return;
        }

        const fs::path newCard0 = folder / "mc0";
        if (!oldCard0.empty() && fs::equivalent(oldCard0, newCard0, ec))
            return;

        // Card 1 sits next to card 0 in both places (runtime: "mc0" -> "mc1").
        // Nothing to copy when the runtime had no card folder at all.
        const fs::path oldCard1 = oldCard0.empty() ? fs::path() : oldCard0.parent_path() / "mc1";
        if (!prepareCard(oldCard0, newCard0) || !prepareCard(oldCard1, folder / "mc1"))
        {
            std::fprintf(stderr, "[saves] memory cards stay in %s\n", oldCard0.string().c_str());
            return;
        }

        paths.mcRoot = newCard0;
        PS2Runtime::setIoPaths(paths);
        std::fprintf(stderr, "[saves] memory card 0: %s\n", newCard0.string().c_str());
    }
}

PS2_REGISTER_GAME_OVERRIDE("Memory cards in a per-user folder", "SLUS_216.61", 0x00100008u, 0u, &ben10::saves::apply);
