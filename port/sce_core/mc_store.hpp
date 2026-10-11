#pragma once

// libmc on directories: card n is <save root>/mc<n>, and every command finishes inside the call
// that issues it. The game still sees libmc's protocol: a call returns 0 when it accepts a command,
// and the next Sync reports the command's function number and its result. Like the IOP's single
// command slot, a new command replaces a result nobody synced.

namespace sce_core::mc {

enum Result {
    kSucceed = 0,
    kNoFormat = -2,
    kFullDevice = -3,
    kNoEntry = -4,
    kDeniedPermit = -5,
    kFailReplace = -8,
    kNoCard = -10,
};

enum CardType {
    kTypeNoCard = 0,
    kTypePs2 = 2,
};

enum OpenFlag {
    kOpenRead = 0x0001,
    kOpenWrite = 0x0002,
    kOpenCreate = 0x0200,
};

constexpr int kDirEntrySize = 0x40;

int Init();
// Returns 1 and the last command's function and result, or -1 if there is none.
int Sync(int *function, int *result);
int GetInfo(int port, int *type, int *free_clusters, int *formatted);
int Open(int port, const char *name, int flags);
int Close(int fd);
int Read(int fd, void *buffer, int size);
int Write(int fd, const void *buffer, int size);
int Flush(int fd);
int Chdir(int port, const char *name, char *current);
int Mkdir(int port, const char *name);
int Delete(int port, const char *name);
int GetDir(int port, const char *name, unsigned int mode, int count, void *table);
int Format(int port);
int Unformat(int port);

void CloseAll();

} // namespace sce_core::mc
