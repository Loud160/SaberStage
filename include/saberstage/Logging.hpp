#pragma once

#include "beatsaber-hook/shared/utils/logging.hpp"

namespace saberstage {

class Logging final {
public:
    static constexpr auto Logger = Paper::ConstLoggerContext("SaberStage");
};

} // namespace saberstage
