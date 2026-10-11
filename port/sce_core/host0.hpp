#pragma once

// The development kit's host file server (host0: and every other device name) on
// <save root>/host0. Errors are negative errno values, as the IOP's file manager returned them.

namespace sce_core::host0 {

enum OpenFlag {
    kReadOnly = 0x0001,
    kWriteOnly = 0x0002,
    kReadWrite = 0x0003,
    kAppend = 0x0100,
    kCreate = 0x0200,
    kTruncate = 0x0400,
    kExclusive = 0x0800,
};

int Open(const char *name, int flags);
int Close(int fd);
int Lseek(int fd, int offset, int whence);
int Read(int fd, void *buffer, int size);
int Write(int fd, const void *buffer, int size);

} // namespace sce_core::host0
