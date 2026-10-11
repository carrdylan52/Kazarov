#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

// The first start of an installed copy: the data directory is missing or holds no file, so the
// game asks for the disc in the system's file selector (the file-chooser portal inside Flatpak,
// which hands over the chosen file without any filesystem permission) and extracts it itself.

// True when root is not a directory or holds no regular file, as main's data check has it.
bool FirstRunDataMissing(const std::filesystem::path &root);

struct FirstRunProgress {
    std::size_t   files = 0;
    std::size_t   total_files = 0;
    std::uint64_t bytes = 0;
    std::uint64_t total_bytes = 0;
};

using FirstRunTick = std::function<bool(const FirstRunProgress &)>;

// What the first run asks of the game: how it names its data, and how it extracts it.
struct FirstRunGame {
    // Writes the game's data from source, a disc image or a folder of the disc's files, into target and
    // checks every file, throwing a std::exception that says what failed. tick returning false
    // cancels, by a throw too.
    std::function<void(const std::filesystem::path &source, const std::filesystem::path &target,
                       const FirstRunTick &tick)>
        extract;
    // The folder button's label, in capitals, and the folder selector's title.
    std::string folder_label;
    std::string folder_prompt;
    std::string image_prompt;
    // The command a Flatpak user runs instead from a shell.
    std::string tool;
};

struct FirstRunOutcome {
    bool        ok = false;
    bool        cancelled = false;
    std::string error;
};

// Extracts source into root on a worker thread with game.extract. It writes to "<root>.partial" and
// renames that over root only once all of it checks out, so a run cut short never leaves a directory
// the game would take for complete; a later run resumes from what is there. tick runs on the calling thread with the latest
// progress while the worker runs; returning false cancels.
FirstRunOutcome FirstRunExtract(const FirstRunGame &game, const std::filesystem::path &source,
                                const std::filesystem::path &root, const FirstRunTick &tick);

enum class FirstRunSource {
    DiscImage,
    Folder,
};

// Stands in for the file selector: returns the chosen path, or nothing for a cancel, which then
// ends the first run. It also lets a headless run go through the flow.
using FirstRunChooser = std::function<std::optional<std::filesystem::path>(FirstRunSource)>;
void FirstRunSetChooser(FirstRunChooser chooser);

// Does nothing when the data directory has files, or for a headless run without a chooser.
// Otherwise opens a window, asks for the disc and extracts it into PathsDataRoot(), then closes
// the window again and returns, with the data in place or, when the person declined, without it
// (the game's data check then exits with its no-data status and game.tool's command). A failed
// extraction shows the error and exits with status 1.
void FirstRunIfNoData(bool headless, const FirstRunGame &game);
