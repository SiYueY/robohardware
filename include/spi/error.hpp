#pragma once

#include <cerrno>
#include <cstdint>

namespace spi {

enum class Error : int {
    // Linux errno values.
    InvalidArgument = EINVAL,
    MessageTooLong = EMSGSIZE,
    BadFileDescriptor = EBADF,
    BadAddress = EFAULT,
    InappropriateIoControlOperation = ENOTTY,
    OperationNotSupported = EOPNOTSUPP,
    PermissionDenied = EACCES,
    OperationNotPermitted = EPERM,
    NoSuchFileOrDirectory = ENOENT,
    NoSuchDevice = ENODEV,
    NoSuchDeviceOrAddress = ENXIO,
    DeviceOrResourceBusy = EBUSY,
    InputOutputError = EIO,
    Interrupted = EINTR,
    IsADirectory = EISDIR,
    TooManyLevelsOfSymbolicLinks = ELOOP,
    TooManyOpenFiles = EMFILE,
    TooManyOpenFilesInSystem = ENFILE,
    FileNameTooLong = ENAMETOOLONG,
    NotADirectory = ENOTDIR,
    CannotAllocateMemory = ENOMEM,
    NoSpaceLeftOnDevice = ENOSPC,
    DiskQuotaExceeded = EDQUOT,
    ValueTooLargeForDefinedDataType = EOVERFLOW,
    ReadOnlyFileSystem = EROFS,
    TextFileBusy = ETXTBSY,

    // Device-local errors use negative values to avoid errno collisions.
    AlreadyOpen = -1,
    NotOpen = -2,
    ConfigurationMismatch = -3,
    TransferMismatch = -4,
};

}
