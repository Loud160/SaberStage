// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: bounded public-provider fetches; callable only from workers.
#pragma once
#include "saberstage/broadcast/SongRequests.hpp"
#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
#include <functional>
namespace saberstage::broadcast {
using DownloadProgress = std::function<void(std::size_t received, std::int64_t expected)>;
[[nodiscard]] std::string FetchChatResource(std::string_view url, std::size_t maximumBytes,
                                            const std::atomic<bool> &stop, std::string_view headers = {},
                                            DownloadProgress progress = {}, int deadlineSeconds = 30);
[[nodiscard]] RequestedMap LookupRequestedMap(std::string_view key, const std::atomic<bool> &stop);
} // namespace saberstage::broadcast
