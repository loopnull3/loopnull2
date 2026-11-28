// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include "core/file_sys/vfs/vfs_types.h"

namespace FileSys {

struct NcaProbe {
    /// Lines for a log: what the file holds where an NCA has its header and file systems.
    std::vector<std::string> lines;
    /// One line of outcome, for a dialog.
    std::string verdict;
};

/// What a content file that does not open as an NCA holds, read as it is stored and with every
/// installed key set. Written for a Switch 2's own files, which carry NHR0 where an NCA has its
/// magic and which nothing here reads: every check is one that cannot pass by chance (a hash the
/// file itself gives), so the lines say how far such a file is laid out as an NCA and whether
/// the installed keys open its content.
NcaProbe ProbeNca(const VirtualFile& file);

} // namespace FileSys
