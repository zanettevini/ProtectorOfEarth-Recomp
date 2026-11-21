// save_location.cpp: the memory cards live in their own "saves" folder inside the game folder.
//
// WHAT IT IS FOR
//   PS2Recomp's runtime keeps memory card 0 in "mc0/" (and card 1 in
//   "mc1/") directly next to the game's ELF, so here in game/, mixed with
//   the files taken from the disc (KNOWN_ISSUES R21).
//
// WHAT IT DOES
//   Keeps the cards with the game's files, in a folder of their own:
//     - BEN10_SAVE_DIR, if set (the cards go in BEN10_SAVE_DIR/mc0 and mc1);
//     - otherwise "saves" next to the ELF: game/saves/mc0 and game/saves/mc1.
//   On first use, when a card folder does not exist in the new place but
//   exists directly next to the ELF (the runtime's old place), it is copied
//   there; the old copy is left as it is (AGENTS.md s.6: saves are copied,
//   never moved). If the folder cannot be created or the copy fails, the
//   cards stay where the runtime puts them, and the log says why.
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
        // Wide API: folder names may contain characters outside the ANSI code page.
        std::wstring wideName(name, name + std::char_traits<char>::length(name));
        const wchar_t *value = _wgetenv(wideName.c_str());
        return (value != nullptr && *value != L'\0') ? fs::path(value) : fs::path();
#else
        const char *value = std::getenv(name);
        return (value != nullptr && *value != '\0') ? fs::path(value) : fs::path();
#endif
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
        const fs::path oldCard0 = paths.mcRoot; // <elf dir>/mc0, set by loadELF

        fs::path folder = envPath("BEN10_SAVE_DIR");
        if (folder.empty())
        {
            if (paths.elfDirectory.empty())
            {
                std::fprintf(stderr, "[saves] the game folder is not known; memory cards stay in %s\n",
                             oldCard0.string().c_str());
                return;
            }
            folder = paths.elfDirectory / "saves";
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

PS2_REGISTER_GAME_OVERRIDE("Memory cards in the game folder's saves/", "SLUS_216.61", 0x00100008u, 0u, &ben10::saves::apply);
