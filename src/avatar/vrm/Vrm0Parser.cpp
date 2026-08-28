#include "saberstage/avatar/vrm/Vrm0Parser.hpp"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace saberstage::avatar::vrm {
namespace {

constexpr std::uint32_t kGlbMagic = 0x46546C67U;
constexpr std::uint32_t kJsonChunk = 0x4E4F534AU;
constexpr std::uint32_t kBinChunk = 0x004E4942U;

class ParseFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) throw ParseFailure("truncated 32-bit GLB field");
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

const rapidjson::Value* Member(const rapidjson::Value& object, const char* name) {
    if (!object.IsObject()) return nullptr;
    const auto it = object.FindMember(name);
    return it == object.MemberEnd() ? nullptr : &it->value;
}

const rapidjson::Value& RequireMember(const rapidjson::Value& object, const char* name, std::string_view context) {
    const auto* value = Member(object, name);
    if (!value) throw ParseFailure(std::string(context) + " is missing '" + name + "'");
    return *value;
}

const rapidjson::Value& RequireArray(const rapidjson::Value& object, const char* name, std::string_view context) {
    const auto& value = RequireMember(object, name, context);
    if (!value.IsArray()) throw ParseFailure(std::string(context) + "." + name + " must be an array");
    return value;
}

std::string StringValue(const rapidjson::Value* value, std::string fallback = {}) {
    return value && value->IsString() ? std::string(value->GetString(), value->GetStringLength()) : std::move(fallback);
}

float FloatValue(const rapidjson::Value* value, float fallback = 0.0F) {
    if (!value || !value->IsNumber()) return fallback;
    const auto result = value->GetFloat();
    return std::isfinite(result) ? result : fallback;
}

bool BoolValue(const rapidjson::Value* value, bool fallback = false) {
    return value && value->IsBool() ? value->GetBool() : fallback;
}

std::size_t CheckedIndex(const rapidjson::Value& value, std::string_view context) {
    if (!value.IsUint64()) throw ParseFailure(std::string(context) + " must be a non-negative integer");
    const auto raw = value.GetUint64();
    if (raw > std::numeric_limits<std::size_t>::max()) throw ParseFailure(std::string(context) + " is too large");
    return static_cast<std::size_t>(raw);
}

std::optional<std::size_t> OptionalIndex(const rapidjson::Value& object, const char* name, std::string_view context) {
    const auto* value = Member(object, name);
    return value ? std::optional<std::size_t>(CheckedIndex(*value, std::string(context) + "." + name)) : std::nullopt;
}

std::optional<std::size_t> OptionalVrmIndex(const rapidjson::Value& object, const char* name, std::string_view context) {
    const auto* value = Member(object, name);
    if (!value || (value->IsInt64() && value->GetInt64() == -1)) return std::nullopt;
    return CheckedIndex(*value, std::string(context) + "." + name);
}

Float3 JsonFloat3(const rapidjson::Value* value, Float3 fallback = {}) {
    if (!value) return fallback;
    Float3 result{};
    if (value->IsArray() && value->Size() >= 3 &&
        (*value)[0].IsNumber() && (*value)[1].IsNumber() && (*value)[2].IsNumber()) {
        result = {(*value)[0].GetFloat(), (*value)[1].GetFloat(), (*value)[2].GetFloat()};
    } else if (value->IsObject()) {
        const auto* x = Member(*value, "x");
        const auto* y = Member(*value, "y");
        const auto* z = Member(*value, "z");
        if (!x || !y || !z || !x->IsNumber() || !y->IsNumber() || !z->IsNumber()) return fallback;
        result = {x->GetFloat(), y->GetFloat(), z->GetFloat()};
    } else {
        return fallback;
    }
    return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z) ? result : fallback;
}

Float4 JsonFloat4(const rapidjson::Value* value, Float4 fallback = {}) {
    if (!value || !value->IsArray()) return fallback;
    Float4 result = fallback;
    auto* fields = &result.x;
    for (rapidjson::SizeType i = 0; i < std::min<rapidjson::SizeType>(4, value->Size()); ++i) {
        if (!(*value)[i].IsNumber()) return fallback;
        fields[i] = (*value)[i].GetFloat();
        if (!std::isfinite(fields[i])) return fallback;
    }
    return result;
}

std::uint32_t ReadBigEndianU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) return 0;
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::pair<std::uint32_t, std::uint32_t> ImageDimensions(std::span<const std::uint8_t> bytes, std::string_view mime) {
    if (mime == "image/png" && bytes.size() >= 24 &&
        bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
        return {ReadBigEndianU32(bytes, 16), ReadBigEndianU32(bytes, 20)};
    }
    if ((mime == "image/jpeg" || mime == "image/jpg") && bytes.size() >= 4 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        std::size_t offset = 2;
        while (offset + 4 <= bytes.size()) {
            if (bytes[offset] != 0xFF) { ++offset; continue; }
            while (offset < bytes.size() && bytes[offset] == 0xFF) ++offset;
            if (offset >= bytes.size()) break;
            const auto marker = bytes[offset++];
            if (marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7)) continue;
            if (offset + 2 > bytes.size()) break;
            const auto length = (static_cast<std::size_t>(bytes[offset]) << 8U) | bytes[offset + 1];
            if (length < 2 || offset + length > bytes.size()) break;
            const bool startOfFrame = (marker >= 0xC0 && marker <= 0xC3) ||
                                      (marker >= 0xC5 && marker <= 0xC7) ||
                                      (marker >= 0xC9 && marker <= 0xCB) ||
                                      (marker >= 0xCD && marker <= 0xCF);
            if (startOfFrame && length >= 7) {
                return {
                    (static_cast<std::uint32_t>(bytes[offset + 5]) << 8U) | bytes[offset + 6],
                    (static_cast<std::uint32_t>(bytes[offset + 3]) << 8U) | bytes[offset + 4]};
            }
            offset += length;
        }
    }
    return {};
}

struct BufferView {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::size_t stride = 0;
};

struct Accessor {
    std::size_t bufferView = 0;
    std::size_t offset = 0;
    std::size_t count = 0;
    std::uint32_t componentType = 0;
    std::string type;
    bool normalized = false;
};

std::size_t ComponentBytes(std::uint32_t componentType) {
    switch (componentType) {
        case 5120: case 5121: return 1;
        case 5122: case 5123: return 2;
        case 5125: case 5126: return 4;
        default: throw ParseFailure("accessor has unsupported componentType " + std::to_string(componentType));
    }
}

std::size_t ComponentCount(std::string_view type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    throw ParseFailure("accessor has unsupported type '" + std::string(type) + "'");
}

template <typename T>
T CopyScalar(const std::uint8_t* bytes) {
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    std::memcpy(&value, bytes, sizeof(T));
    return value;
}

double DecodeComponent(const std::uint8_t* bytes, std::uint32_t type, bool normalized) {
    switch (type) {
        case 5120: {
            const auto value = CopyScalar<std::int8_t>(bytes);
            return normalized ? std::max(-1.0, static_cast<double>(value) / 127.0) : value;
        }
        case 5121: {
            const auto value = CopyScalar<std::uint8_t>(bytes);
            return normalized ? static_cast<double>(value) / 255.0 : value;
        }
        case 5122: {
            const auto value = CopyScalar<std::int16_t>(bytes);
            return normalized ? std::max(-1.0, static_cast<double>(value) / 32767.0) : value;
        }
        case 5123: {
            const auto value = CopyScalar<std::uint16_t>(bytes);
            return normalized ? static_cast<double>(value) / 65535.0 : value;
        }
        case 5125: {
            const auto value = CopyScalar<std::uint32_t>(bytes);
            return normalized ? static_cast<double>(value) / 4294967295.0 : value;
        }
        case 5126: {
            const auto value = CopyScalar<float>(bytes);
            if (!std::isfinite(value)) throw ParseFailure("accessor contains a non-finite float");
            return value;
        }
        default: throw ParseFailure("unsupported accessor component type");
    }
}

class Parser final {
public:
    Parser(std::span<const std::uint8_t> bytes, std::string label, const AssetLimits& limits)
        : file_(bytes), limits_(limits) { asset_.sourceLabel = std::move(label); }

    ParseResult Run() {
        ParseGlb();
        ParseRoot();
        ValidateCrossReferences();
        asset_.statistics.fileBytes = file_.size();
        asset_.statistics.nodeCount = asset_.nodes.size();
        asset_.statistics.meshCount = asset_.meshes.size();
        asset_.statistics.skinCount = asset_.skins.size();
        asset_.statistics.materialCount = asset_.materials.size();
        asset_.statistics.textureCount = asset_.textures.size();
        asset_.statistics.imageCount = asset_.images.size();
        return {std::move(asset_), {}, std::move(warnings_)};
    }

private:
    void ParseGlb() {
        if (file_.size() > limits_.maximumFileBytes) throw ParseFailure("file exceeds configured byte limit");
        if (file_.size() < 20) throw ParseFailure("file is too small to be a GLB");
        if (ReadU32(file_, 0) != kGlbMagic) throw ParseFailure("file is not a binary glTF (GLB)");
        if (ReadU32(file_, 4) != 2) throw ParseFailure("only GLB version 2 is supported");
        if (ReadU32(file_, 8) != file_.size()) throw ParseFailure("GLB declared length does not match file size");

        std::size_t offset = 12;
        bool foundJson = false;
        while (offset < file_.size()) {
            if (file_.size() - offset < 8) throw ParseFailure("truncated GLB chunk header");
            const auto length = static_cast<std::size_t>(ReadU32(file_, offset));
            const auto type = ReadU32(file_, offset + 4);
            offset += 8;
            if (length > file_.size() - offset) throw ParseFailure("GLB chunk exceeds file bounds");
            if (type == kJsonChunk) {
                if (foundJson) throw ParseFailure("GLB has multiple JSON chunks");
                if (length > limits_.maximumJsonBytes) throw ParseFailure("GLB JSON exceeds configured byte limit");
                document_.Parse(reinterpret_cast<const char*>(file_.data() + offset), length);
                if (document_.HasParseError()) {
                    throw ParseFailure(std::string("invalid glTF JSON at byte ") +
                        std::to_string(document_.GetErrorOffset()) + ": " +
                        rapidjson::GetParseError_En(document_.GetParseError()));
                }
                foundJson = true;
            } else if (type == kBinChunk) {
                if (!binary_.empty()) throw ParseFailure("GLB has multiple BIN chunks");
                if (length > limits_.maximumBinaryBytes) throw ParseFailure("GLB BIN exceeds configured byte limit");
                binary_ = file_.subspan(offset, length);
            }
            offset += length;
        }
        if (!foundJson || !document_.IsObject()) throw ParseFailure("GLB is missing an object JSON chunk");
        if (binary_.empty()) throw ParseFailure("GLB is missing its BIN chunk");
    }

    void ParseRoot() {
        if (const auto* asset = Member(document_, "asset")) {
            asset_.generator = StringValue(Member(*asset, "generator"));
            if (StringValue(Member(*asset, "version")) != "2.0") throw ParseFailure("glTF asset.version must be 2.0");
        } else {
            throw ParseFailure("glTF asset metadata is missing");
        }
        const auto* extensions = Member(document_, "extensions");
        const auto* vrm = extensions ? Member(*extensions, "VRM") : nullptr;
        if (!vrm || !vrm->IsObject()) throw ParseFailure("VRM 0.x extension is missing");

        static const std::unordered_set<std::string> supportedExtensions{"VRM", "KHR_materials_unlit"};
        if (const auto* used = Member(document_, "extensionsUsed")) {
            if (!used->IsArray()) throw ParseFailure("glTF.extensionsUsed must be an array");
            for (const auto& extension : used->GetArray()) {
                if (!extension.IsString()) throw ParseFailure("glTF.extensionsUsed entries must be strings");
                const std::string name(extension.GetString(), extension.GetStringLength());
                if (!supportedExtensions.contains(name)) warnings_.push_back("optional glTF extension is not interpreted: " + name);
            }
        }
        if (const auto* required = Member(document_, "extensionsRequired")) {
            if (!required->IsArray()) throw ParseFailure("glTF.extensionsRequired must be an array");
            for (const auto& extension : required->GetArray()) {
                if (!extension.IsString()) throw ParseFailure("glTF.extensionsRequired entries must be strings");
                const std::string name(extension.GetString(), extension.GetStringLength());
                if (!supportedExtensions.contains(name)) throw ParseFailure("required glTF extension is unsupported: " + name);
            }
        }

        ParseBufferViews();
        ParseAccessors();
        ParseSamplers();
        ParseImages();
        ParseTextures();
        ParseMaterials(*vrm);
        ParseMeshes();
        ParseSkins();
        ParseNodes();
        ParseScenes();
        ParseVrm(*vrm);
    }

    void ParseBufferViews() {
        const auto& views = RequireArray(document_, "bufferViews", "glTF");
        bufferViews_.reserve(views.Size());
        for (rapidjson::SizeType i = 0; i < views.Size(); ++i) {
            const auto& value = views[i];
            if (!value.IsObject()) throw ParseFailure("bufferView must be an object");
            const auto buffer = OptionalIndex(value, "buffer", "bufferView").value_or(0);
            if (buffer != 0) throw ParseFailure("external/multiple glTF buffers are not supported");
            BufferView view{};
            view.offset = OptionalIndex(value, "byteOffset", "bufferView").value_or(0);
            view.length = CheckedIndex(RequireMember(value, "byteLength", "bufferView"), "bufferView.byteLength");
            view.stride = OptionalIndex(value, "byteStride", "bufferView").value_or(0);
            if (view.offset > binary_.size() || view.length > binary_.size() - view.offset) {
                throw ParseFailure("bufferView exceeds GLB BIN bounds");
            }
            bufferViews_.push_back(view);
        }
    }

    void ParseAccessors() {
        const auto& values = RequireArray(document_, "accessors", "glTF");
        accessors_.reserve(values.Size());
        for (rapidjson::SizeType i = 0; i < values.Size(); ++i) {
            const auto& value = values[i];
            if (!value.IsObject()) throw ParseFailure("accessor must be an object");
            if (Member(value, "sparse")) throw ParseFailure("sparse accessors are not supported in this milestone");
            Accessor accessor{};
            accessor.bufferView = CheckedIndex(RequireMember(value, "bufferView", "accessor"), "accessor.bufferView");
            accessor.offset = OptionalIndex(value, "byteOffset", "accessor").value_or(0);
            accessor.count = CheckedIndex(RequireMember(value, "count", "accessor"), "accessor.count");
            const auto& component = RequireMember(value, "componentType", "accessor");
            if (!component.IsUint()) throw ParseFailure("accessor.componentType must be an unsigned integer");
            accessor.componentType = component.GetUint();
            accessor.type = StringValue(&RequireMember(value, "type", "accessor"));
            accessor.normalized = BoolValue(Member(value, "normalized"));
            if (accessor.bufferView >= bufferViews_.size()) throw ParseFailure("accessor references an invalid bufferView");
            const auto elementBytes = ComponentBytes(accessor.componentType) * ComponentCount(accessor.type);
            const auto stride = bufferViews_[accessor.bufferView].stride == 0 ? elementBytes : bufferViews_[accessor.bufferView].stride;
            if (stride < elementBytes) throw ParseFailure("accessor byteStride is smaller than its element");
            const auto& view = bufferViews_[accessor.bufferView];
            if (accessor.offset > view.length || (accessor.count != 0 &&
                (accessor.count - 1 > (std::numeric_limits<std::size_t>::max() - elementBytes) / stride ||
                 accessor.offset + (accessor.count - 1) * stride + elementBytes > view.length))) {
                throw ParseFailure("accessor exceeds its bufferView bounds");
            }
            accessors_.push_back(std::move(accessor));
        }
    }

    const Accessor& GetAccessor(std::size_t index, std::string_view expectedType) const {
        if (index >= accessors_.size()) throw ParseFailure("attribute references an invalid accessor");
        const auto& accessor = accessors_[index];
        if (accessor.type != expectedType) throw ParseFailure("accessor type is " + accessor.type + ", expected " + std::string(expectedType));
        return accessor;
    }

    std::vector<double> Decode(std::size_t index, std::string_view expectedType) const {
        const auto& accessor = GetAccessor(index, expectedType);
        const auto components = ComponentCount(accessor.type);
        if (accessor.count > std::numeric_limits<std::size_t>::max() / components) throw ParseFailure("accessor decoded size overflows");
        std::vector<double> result(accessor.count * components);
        const auto& view = bufferViews_[accessor.bufferView];
        const auto componentBytes = ComponentBytes(accessor.componentType);
        const auto elementBytes = componentBytes * components;
        const auto stride = view.stride == 0 ? elementBytes : view.stride;
        const auto* start = binary_.data() + view.offset + accessor.offset;
        for (std::size_t element = 0; element < accessor.count; ++element) {
            for (std::size_t component = 0; component < components; ++component) {
                result[element * components + component] = DecodeComponent(
                    start + element * stride + component * componentBytes,
                    accessor.componentType,
                    accessor.normalized);
            }
        }
        return result;
    }

    std::vector<Float2> Float2s(std::size_t index) const {
        const auto decoded = Decode(index, "VEC2");
        std::vector<Float2> result(decoded.size() / 2);
        for (std::size_t i = 0; i < result.size(); ++i) result[i] = {static_cast<float>(decoded[i * 2]), static_cast<float>(decoded[i * 2 + 1])};
        return result;
    }

    std::vector<Float3> Float3s(std::size_t index) const {
        const auto decoded = Decode(index, "VEC3");
        std::vector<Float3> result(decoded.size() / 3);
        for (std::size_t i = 0; i < result.size(); ++i) result[i] = {
            static_cast<float>(decoded[i * 3]), static_cast<float>(decoded[i * 3 + 1]), static_cast<float>(decoded[i * 3 + 2])};
        return result;
    }

    std::vector<Float4> Float4s(std::size_t index) const {
        const auto decoded = Decode(index, "VEC4");
        std::vector<Float4> result(decoded.size() / 4);
        for (std::size_t i = 0; i < result.size(); ++i) result[i] = {
            static_cast<float>(decoded[i * 4]), static_cast<float>(decoded[i * 4 + 1]),
            static_cast<float>(decoded[i * 4 + 2]), static_cast<float>(decoded[i * 4 + 3])};
        return result;
    }

    std::vector<UInt4> UInt4s(std::size_t index) const {
        const auto& accessor = GetAccessor(index, "VEC4");
        if (accessor.componentType != 5121 && accessor.componentType != 5123) {
            throw ParseFailure("JOINTS_0 must use unsigned byte or unsigned short components");
        }
        const auto decoded = Decode(index, "VEC4");
        std::vector<UInt4> result(decoded.size() / 4);
        for (std::size_t i = 0; i < result.size(); ++i) result[i] = {
            static_cast<std::uint16_t>(decoded[i * 4]), static_cast<std::uint16_t>(decoded[i * 4 + 1]),
            static_cast<std::uint16_t>(decoded[i * 4 + 2]), static_cast<std::uint16_t>(decoded[i * 4 + 3])};
        return result;
    }

    std::vector<std::uint32_t> Indices(std::size_t index) const {
        const auto& accessor = GetAccessor(index, "SCALAR");
        if (accessor.componentType != 5121 && accessor.componentType != 5123 && accessor.componentType != 5125) {
            throw ParseFailure("indices must use unsigned byte, unsigned short, or unsigned int components");
        }
        const auto decoded = Decode(index, "SCALAR");
        std::vector<std::uint32_t> result(decoded.size());
        for (std::size_t i = 0; i < result.size(); ++i) result[i] = static_cast<std::uint32_t>(decoded[i]);
        return result;
    }

    std::vector<Matrix4> Matrices(std::size_t index) const {
        const auto& accessor = GetAccessor(index, "MAT4");
        if (accessor.componentType != 5126) throw ParseFailure("inverse bind matrices must use float components");
        const auto decoded = Decode(index, "MAT4");
        std::vector<Matrix4> result(decoded.size() / 16);
        for (std::size_t i = 0; i < result.size(); ++i) {
            for (std::size_t j = 0; j < 16; ++j) result[i].values[j] = static_cast<float>(decoded[i * 16 + j]);
        }
        return result;
    }

    void ParseSamplers() {
        const auto* values = Member(document_, "samplers");
        if (!values) return;
        if (!values->IsArray()) throw ParseFailure("glTF.samplers must be an array");
        asset_.samplers.reserve(values->Size());
        for (const auto& value : values->GetArray()) {
            if (!value.IsObject()) throw ParseFailure("sampler must be an object");
            Sampler sampler{};
            if (const auto* v = Member(value, "magFilter"); v && v->IsInt()) sampler.magFilter = v->GetInt();
            if (const auto* v = Member(value, "minFilter"); v && v->IsInt()) sampler.minFilter = v->GetInt();
            if (const auto* v = Member(value, "wrapS"); v && v->IsInt()) sampler.wrapS = v->GetInt();
            if (const auto* v = Member(value, "wrapT"); v && v->IsInt()) sampler.wrapT = v->GetInt();
            asset_.samplers.push_back(sampler);
        }
    }

    void ParseImages() {
        const auto* values = Member(document_, "images");
        if (!values) return;
        if (!values->IsArray()) throw ParseFailure("glTF.images must be an array");
        if (values->Size() > limits_.maximumImages) throw ParseFailure("image count exceeds configured limit");
        asset_.images.reserve(values->Size());
        for (const auto& value : values->GetArray()) {
            if (!value.IsObject()) throw ParseFailure("image must be an object");
            if (Member(value, "uri")) throw ParseFailure("external/data-URI images are not supported; VRM must be self-contained");
            const auto viewIndex = CheckedIndex(RequireMember(value, "bufferView", "image"), "image.bufferView");
            if (viewIndex >= bufferViews_.size()) throw ParseFailure("image references an invalid bufferView");
            const auto& view = bufferViews_[viewIndex];
            if (view.length > limits_.maximumImageEncodedBytes) throw ParseFailure("encoded image exceeds configured byte limit");
            Image image{};
            image.name = StringValue(Member(value, "name"));
            image.mimeType = StringValue(Member(value, "mimeType"));
            if (image.mimeType != "image/png" && image.mimeType != "image/jpeg" && image.mimeType != "image/jpg") {
                throw ParseFailure("image has unsupported MIME type '" + image.mimeType + "'");
            }
            image.encoded.assign(binary_.begin() + static_cast<std::ptrdiff_t>(view.offset),
                                 binary_.begin() + static_cast<std::ptrdiff_t>(view.offset + view.length));
            std::tie(image.encodedWidth, image.encodedHeight) = ImageDimensions(image.encoded, image.mimeType);
            if (image.encodedWidth == 0 || image.encodedHeight == 0) throw ParseFailure("could not validate encoded image dimensions");
            if (image.encodedWidth > limits_.maximumImageDimension || image.encodedHeight > limits_.maximumImageDimension) {
                throw ParseFailure("encoded image dimensions exceed configured limit");
            }
            asset_.statistics.encodedImageBytes += image.encoded.size();
            asset_.statistics.decodedImageBytesAtSourceSize +=
                static_cast<std::size_t>(image.encodedWidth) * image.encodedHeight * 4U;
            asset_.images.push_back(std::move(image));
        }
    }

    void ParseTextures() {
        const auto* values = Member(document_, "textures");
        if (!values) return;
        if (!values->IsArray()) throw ParseFailure("glTF.textures must be an array");
        asset_.textures.reserve(values->Size());
        for (const auto& value : values->GetArray()) {
            if (!value.IsObject()) throw ParseFailure("texture must be an object");
            Texture texture{};
            texture.name = StringValue(Member(value, "name"));
            texture.source = CheckedIndex(RequireMember(value, "source", "texture"), "texture.source");
            texture.sampler = OptionalIndex(value, "sampler", "texture");
            if (texture.source >= asset_.images.size()) throw ParseFailure("texture references an invalid image");
            if (texture.sampler && *texture.sampler >= asset_.samplers.size()) throw ParseFailure("texture references an invalid sampler");
            asset_.textures.push_back(std::move(texture));
        }
    }

    void ParseMaterials(const rapidjson::Value& vrm) {
        const auto* gltfMaterials = Member(document_, "materials");
        const auto* vrmMaterials = Member(vrm, "materialProperties");
        const auto count = std::max(
            gltfMaterials && gltfMaterials->IsArray() ? gltfMaterials->Size() : 0U,
            vrmMaterials && vrmMaterials->IsArray() ? vrmMaterials->Size() : 0U);
        asset_.materials.resize(count);
        for (rapidjson::SizeType i = 0; i < count; ++i) {
            auto& material = asset_.materials[i];
            if (gltfMaterials && gltfMaterials->IsArray() && i < gltfMaterials->Size()) {
                const auto& source = (*gltfMaterials)[i];
                material.name = StringValue(Member(source, "name"), "Material " + std::to_string(i));
                if (const auto* pbr = Member(source, "pbrMetallicRoughness")) {
                    material.vectorProperties["_Color"] = JsonFloat4(Member(*pbr, "baseColorFactor"), {1, 1, 1, 1});
                    if (const auto* texture = Member(*pbr, "baseColorTexture")) {
                        if (const auto index = OptionalIndex(*texture, "index", "baseColorTexture")) material.textureProperties["_MainTex"] = *index;
                    }
                }
            }
            if (!vrmMaterials || !vrmMaterials->IsArray() || i >= vrmMaterials->Size()) continue;
            const auto& source = (*vrmMaterials)[i];
            if (!source.IsObject()) throw ParseFailure("VRM materialProperty must be an object");
            material.name = StringValue(Member(source, "name"), material.name);
            material.shader = StringValue(Member(source, "shader"));
            if (!material.shader.empty() && material.shader != "VRM/MToon" && material.shader != "VRM/UnlitTexture" &&
                material.shader != "VRM/UnlitTransparent" && material.shader != "VRM/UnlitCutout") {
                const auto warning = "VRM material shader will use the first-pass fallback: " + material.shader;
                if (std::find(warnings_.begin(), warnings_.end(), warning) == warnings_.end()) warnings_.push_back(warning);
            }
            if (const auto* properties = Member(source, "floatProperties")) {
                if (!properties->IsObject()) throw ParseFailure("floatProperties must be an object");
                for (auto it = properties->MemberBegin(); it != properties->MemberEnd(); ++it) {
                    if (it->value.IsNumber()) material.floatProperties[it->name.GetString()] = it->value.GetFloat();
                }
            }
            if (const auto* properties = Member(source, "vectorProperties")) {
                if (!properties->IsObject()) throw ParseFailure("vectorProperties must be an object");
                for (auto it = properties->MemberBegin(); it != properties->MemberEnd(); ++it) {
                    material.vectorProperties[it->name.GetString()] = JsonFloat4(&it->value);
                }
            }
            if (const auto* properties = Member(source, "textureProperties")) {
                if (!properties->IsObject()) throw ParseFailure("textureProperties must be an object");
                for (auto it = properties->MemberBegin(); it != properties->MemberEnd(); ++it) {
                    if (it->value.IsUint64()) material.textureProperties[it->name.GetString()] = CheckedIndex(it->value, "texture property");
                }
            }
            const auto parseStringMap = [&](const char* field, auto& destination) {
                const auto* properties = Member(source, field);
                if (!properties) return;
                if (!properties->IsObject()) throw ParseFailure(std::string(field) + " must be an object");
                for (auto it = properties->MemberBegin(); it != properties->MemberEnd(); ++it) {
                    if (it->value.IsString()) destination[it->name.GetString()] = it->value.GetString();
                    else if (it->value.IsBool()) destination[it->name.GetString()] = it->value.GetBool() ? "true" : "false";
                }
            };
            parseStringMap("keywordMap", material.keywordMap);
            parseStringMap("tagMap", material.tagMap);
        }
        for (const auto& material : asset_.materials) {
            for (const auto& [name, texture] : material.textureProperties) {
                if (texture >= asset_.textures.size()) throw ParseFailure("material texture property '" + name + "' is out of range");
            }
        }
    }

    void ParseMeshes() {
        const auto& values = RequireArray(document_, "meshes", "glTF");
        if (values.Size() > limits_.maximumMeshes) throw ParseFailure("mesh count exceeds configured limit");
        asset_.meshes.reserve(values.Size());
        for (rapidjson::SizeType meshIndex = 0; meshIndex < values.Size(); ++meshIndex) {
            const auto& value = values[meshIndex];
            if (!value.IsObject()) throw ParseFailure("mesh must be an object");
            Mesh mesh{};
            mesh.name = StringValue(Member(value, "name"), "Mesh " + std::to_string(meshIndex));
            if (const auto* weights = Member(value, "weights")) {
                if (!weights->IsArray()) throw ParseFailure("mesh.weights must be an array");
                for (const auto& weight : weights->GetArray()) mesh.initialMorphWeights.push_back(FloatValue(&weight));
            }
            if (const auto* extras = Member(value, "extras")) {
                if (const auto* names = Member(*extras, "targetNames")) {
                    if (!names->IsArray()) throw ParseFailure("mesh.extras.targetNames must be an array");
                    for (const auto& name : names->GetArray()) mesh.morphTargetNames.push_back(StringValue(&name));
                }
            }
            const auto& primitives = RequireArray(value, "primitives", "mesh");
            if (asset_.statistics.primitiveCount + primitives.Size() > limits_.maximumPrimitives) {
                throw ParseFailure("primitive count exceeds configured limit");
            }
            mesh.primitives.reserve(primitives.Size());
            for (const auto& source : primitives.GetArray()) {
                if (!source.IsObject()) throw ParseFailure("mesh primitive must be an object");
                Primitive primitive{};
                if (const auto* mode = Member(source, "mode")) {
                    if (!mode->IsInt()) throw ParseFailure("primitive.mode must be an integer");
                    primitive.mode = mode->GetInt();
                }
                if (primitive.mode != 4) throw ParseFailure("only triangle-list primitives are supported");
                primitive.material = OptionalIndex(source, "material", "primitive");
                if (primitive.material && *primitive.material >= asset_.materials.size()) throw ParseFailure("primitive references an invalid material");
                const auto& attributes = RequireMember(source, "attributes", "primitive");
                if (!attributes.IsObject()) throw ParseFailure("primitive.attributes must be an object");
                const auto attributeIndex = [&](const char* name, bool required) -> std::optional<std::size_t> {
                    const auto* attribute = Member(attributes, name);
                    if (!attribute) {
                        if (required) throw ParseFailure(std::string("primitive is missing ") + name);
                        return std::nullopt;
                    }
                    return CheckedIndex(*attribute, std::string("attribute ") + name);
                };
                primitive.positions = Float3s(*attributeIndex("POSITION", true));
                if (const auto accessor = attributeIndex("NORMAL", false)) primitive.normals = Float3s(*accessor);
                if (const auto accessor = attributeIndex("TANGENT", false)) primitive.tangents = Float4s(*accessor);
                if (const auto accessor = attributeIndex("TEXCOORD_0", false)) primitive.texcoords0 = Float2s(*accessor);
                if (const auto accessor = attributeIndex("JOINTS_0", false)) primitive.joints0 = UInt4s(*accessor);
                if (const auto accessor = attributeIndex("WEIGHTS_0", false)) primitive.weights0 = Float4s(*accessor);
                if (const auto* indices = Member(source, "indices")) primitive.indices = Indices(CheckedIndex(*indices, "primitive.indices"));
                else {
                    primitive.indices.resize(primitive.positions.size());
                    for (std::size_t i = 0; i < primitive.indices.size(); ++i) primitive.indices[i] = static_cast<std::uint32_t>(i);
                }
                const auto vertices = primitive.positions.size();
                const auto sameSize = [vertices](std::size_t size) { return size == 0 || size == vertices; };
                if (!sameSize(primitive.normals.size()) || !sameSize(primitive.tangents.size()) ||
                    !sameSize(primitive.texcoords0.size()) || !sameSize(primitive.joints0.size()) || !sameSize(primitive.weights0.size())) {
                    throw ParseFailure("primitive attributes have inconsistent vertex counts");
                }
                if (primitive.joints0.empty() != primitive.weights0.empty()) throw ParseFailure("skinned primitive must provide both JOINTS_0 and WEIGHTS_0");
                for (const auto& weights : primitive.weights0) {
                    if (weights.x < 0.0F || weights.y < 0.0F || weights.z < 0.0F || weights.w < 0.0F) {
                        throw ParseFailure("WEIGHTS_0 contains a negative weight");
                    }
                }
                if (primitive.indices.size() % 3 != 0) throw ParseFailure("triangle-list index count is not divisible by three");
                for (const auto index : primitive.indices) if (index >= vertices) throw ParseFailure("primitive index exceeds vertex count");
                if (asset_.statistics.vertexCount + vertices > limits_.maximumVertices) throw ParseFailure("vertex count exceeds configured limit");
                if (asset_.statistics.triangleCount + primitive.indices.size() / 3 > limits_.maximumTriangles) throw ParseFailure("triangle count exceeds configured limit");

                if (const auto* targets = Member(source, "targets")) {
                    if (!targets->IsArray()) throw ParseFailure("primitive.targets must be an array");
                    if (asset_.statistics.morphTargetCount + targets->Size() > limits_.maximumMorphTargets) {
                        throw ParseFailure("morph target count exceeds configured limit");
                    }
                    primitive.morphTargets.reserve(targets->Size());
                    for (const auto& target : targets->GetArray()) {
                        if (!target.IsObject()) throw ParseFailure("morph target must be an object");
                        MorphTarget morph{};
                        if (const auto* accessor = Member(target, "POSITION")) morph.positionDeltas = Float3s(CheckedIndex(*accessor, "morph POSITION"));
                        if (const auto* accessor = Member(target, "NORMAL")) morph.normalDeltas = Float3s(CheckedIndex(*accessor, "morph NORMAL"));
                        if (const auto* accessor = Member(target, "TANGENT")) morph.tangentDeltas = Float3s(CheckedIndex(*accessor, "morph TANGENT"));
                        if (!sameSize(morph.positionDeltas.size()) || !sameSize(morph.normalDeltas.size()) || !sameSize(morph.tangentDeltas.size())) {
                            throw ParseFailure("morph target has an inconsistent vertex count");
                        }
                        primitive.morphTargets.push_back(std::move(morph));
                    }
                    asset_.statistics.morphTargetCount += targets->Size();
                }
                if (!mesh.morphTargetNames.empty() && mesh.morphTargetNames.size() != primitive.morphTargets.size()) {
                    throw ParseFailure("mesh targetNames count does not match primitive morph target count");
                }
                asset_.statistics.vertexCount += vertices;
                asset_.statistics.triangleCount += primitive.indices.size() / 3;
                ++asset_.statistics.primitiveCount;
                mesh.primitives.push_back(std::move(primitive));
            }
            asset_.meshes.push_back(std::move(mesh));
        }
    }

    void ParseSkins() {
        const auto* values = Member(document_, "skins");
        if (!values) return;
        if (!values->IsArray()) throw ParseFailure("glTF.skins must be an array");
        asset_.skins.reserve(values->Size());
        for (const auto& value : values->GetArray()) {
            if (!value.IsObject()) throw ParseFailure("skin must be an object");
            Skin skin{};
            skin.name = StringValue(Member(value, "name"));
            const auto& joints = RequireArray(value, "joints", "skin");
            if (joints.Empty()) throw ParseFailure("skin must contain at least one joint");
            skin.joints.reserve(joints.Size());
            for (const auto& joint : joints.GetArray()) skin.joints.push_back(CheckedIndex(joint, "skin joint"));
            skin.skeleton = OptionalIndex(value, "skeleton", "skin");
            if (const auto accessor = OptionalIndex(value, "inverseBindMatrices", "skin")) skin.inverseBindMatrices = Matrices(*accessor);
            else skin.inverseBindMatrices.resize(skin.joints.size());
            if (skin.inverseBindMatrices.size() != skin.joints.size()) throw ParseFailure("skin inverseBindMatrices count does not match joints");
            asset_.skins.push_back(std::move(skin));
        }
    }

    static void DecomposeNodeMatrix(const rapidjson::Value& value, Node& node) {
        if (!value.IsArray() || value.Size() != 16) throw ParseFailure("node.matrix must contain 16 numbers");
        std::array<float, 16> m{};
        for (rapidjson::SizeType i = 0; i < 16; ++i) {
            if (!value[i].IsNumber()) throw ParseFailure("node.matrix must contain only numbers");
            m[i] = value[i].GetFloat();
            if (!std::isfinite(m[i])) throw ParseFailure("node.matrix contains a non-finite value");
        }
        node.translation = {m[12], m[13], m[14]};
        const auto length = [](float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); };
        node.scale = {length(m[0], m[1], m[2]), length(m[4], m[5], m[6]), length(m[8], m[9], m[10])};
        if (node.scale.x <= 1.0e-8F || node.scale.y <= 1.0e-8F || node.scale.z <= 1.0e-8F) throw ParseFailure("node.matrix has a zero scale axis");
        float r00 = m[0] / node.scale.x, r01 = m[4] / node.scale.y, r02 = m[8] / node.scale.z;
        float r10 = m[1] / node.scale.x, r11 = m[5] / node.scale.y, r12 = m[9] / node.scale.z;
        float r20 = m[2] / node.scale.x, r21 = m[6] / node.scale.y, r22 = m[10] / node.scale.z;
        const auto determinant = r00 * (r11 * r22 - r12 * r21) - r01 * (r10 * r22 - r12 * r20) + r02 * (r10 * r21 - r11 * r20);
        if (determinant < 0.0F) {
            node.scale.x = -node.scale.x;
            r00 = -r00; r10 = -r10; r20 = -r20;
        }
        Float4 q{};
        const auto trace = r00 + r11 + r22;
        if (trace > 0.0F) {
            const auto s = std::sqrt(trace + 1.0F) * 2.0F;
            q = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25F * s};
        } else if (r00 > r11 && r00 > r22) {
            const auto s = std::sqrt(1.0F + r00 - r11 - r22) * 2.0F;
            q = {0.25F * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
        } else if (r11 > r22) {
            const auto s = std::sqrt(1.0F + r11 - r00 - r22) * 2.0F;
            q = {(r01 + r10) / s, 0.25F * s, (r12 + r21) / s, (r02 - r20) / s};
        } else {
            const auto s = std::sqrt(1.0F + r22 - r00 - r11) * 2.0F;
            q = {(r02 + r20) / s, (r12 + r21) / s, 0.25F * s, (r10 - r01) / s};
        }
        const auto qLength = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (qLength <= 1.0e-8F) throw ParseFailure("node.matrix rotation could not be decomposed");
        node.rotation = {q.x / qLength, q.y / qLength, q.z / qLength, q.w / qLength};
    }

    void ParseNodes() {
        const auto& values = RequireArray(document_, "nodes", "glTF");
        if (values.Size() > limits_.maximumNodes) throw ParseFailure("node count exceeds configured limit");
        asset_.nodes.resize(values.Size());
        for (rapidjson::SizeType i = 0; i < values.Size(); ++i) {
            const auto& value = values[i];
            if (!value.IsObject()) throw ParseFailure("node must be an object");
            auto& node = asset_.nodes[i];
            node.name = StringValue(Member(value, "name"), "Node " + std::to_string(i));
            node.mesh = OptionalIndex(value, "mesh", "node");
            node.skin = OptionalIndex(value, "skin", "node");
            if (const auto* matrix = Member(value, "matrix")) {
                if (Member(value, "translation") || Member(value, "rotation") || Member(value, "scale")) {
                    throw ParseFailure("node cannot contain both matrix and TRS transforms");
                }
                DecomposeNodeMatrix(*matrix, node);
            } else {
                node.translation = JsonFloat3(Member(value, "translation"));
                node.rotation = JsonFloat4(Member(value, "rotation"), {0, 0, 0, 1});
                node.scale = JsonFloat3(Member(value, "scale"), {1, 1, 1});
            }
            if (const auto* children = Member(value, "children")) {
                if (!children->IsArray()) throw ParseFailure("node.children must be an array");
                for (const auto& child : children->GetArray()) node.children.push_back(CheckedIndex(child, "node child"));
            }
        }
        for (std::size_t parent = 0; parent < asset_.nodes.size(); ++parent) {
            std::unordered_set<std::size_t> unique;
            for (const auto child : asset_.nodes[parent].children) {
                if (child >= asset_.nodes.size()) throw ParseFailure("node child reference is out of range");
                if (child == parent) throw ParseFailure("node cannot be its own child");
                if (!unique.insert(child).second) throw ParseFailure("node lists a child more than once");
                if (asset_.nodes[child].parent) throw ParseFailure("node has multiple parents");
                asset_.nodes[child].parent = parent;
            }
        }
        std::vector<std::uint8_t> color(asset_.nodes.size());
        const auto visit = [&](auto&& self, std::size_t node) -> void {
            if (color[node] == 1) throw ParseFailure("node hierarchy contains a cycle");
            if (color[node] == 2) return;
            color[node] = 1;
            for (const auto child : asset_.nodes[node].children) self(self, child);
            color[node] = 2;
        };
        for (std::size_t i = 0; i < asset_.nodes.size(); ++i) visit(visit, i);
    }

    void ParseScenes() {
        const auto* scenes = Member(document_, "scenes");
        if (!scenes || !scenes->IsArray() || scenes->Empty()) {
            for (std::size_t i = 0; i < asset_.nodes.size(); ++i) if (!asset_.nodes[i].parent) asset_.sceneRoots.push_back(i);
            return;
        }
        const auto sceneIndex = OptionalIndex(document_, "scene", "glTF").value_or(0);
        if (sceneIndex >= scenes->Size()) throw ParseFailure("default scene index is out of range");
        const auto& roots = RequireArray((*scenes)[static_cast<rapidjson::SizeType>(sceneIndex)], "nodes", "scene");
        for (const auto& root : roots.GetArray()) {
            const auto index = CheckedIndex(root, "scene root");
            if (index >= asset_.nodes.size()) throw ParseFailure("scene root is out of range");
            asset_.sceneRoots.push_back(index);
        }
    }

    void ParseVrm(const rapidjson::Value& vrm) {
        asset_.vrmExporterVersion = StringValue(Member(vrm, "exporterVersion"));
        asset_.vrmSpecVersion = StringValue(Member(vrm, "specVersion"));
        if (asset_.vrmSpecVersion.empty() || asset_.vrmSpecVersion[0] != '0') throw ParseFailure("only VRM 0.x is supported");
        if (const auto* meta = Member(vrm, "meta")) {
            asset_.meta.title = StringValue(Member(*meta, "title"));
            asset_.meta.version = StringValue(Member(*meta, "version"));
            asset_.meta.author = StringValue(Member(*meta, "author"));
            asset_.meta.contactInformation = StringValue(Member(*meta, "contactInformation"));
            asset_.meta.reference = StringValue(Member(*meta, "reference"));
            asset_.meta.licenseName = StringValue(Member(*meta, "licenseName"));
            asset_.meta.otherLicenseUrl = StringValue(Member(*meta, "otherLicenseUrl"));
            asset_.meta.thumbnailTexture = OptionalIndex(*meta, "texture", "VRM.meta");
        }
        if (asset_.meta.thumbnailTexture) {
            if (*asset_.meta.thumbnailTexture >= asset_.textures.size()) throw ParseFailure("VRM thumbnail texture is out of range");
            asset_.images[asset_.textures[*asset_.meta.thumbnailTexture].source].thumbnailOnly = true;
        }

        const auto& humanoid = RequireMember(vrm, "humanoid", "VRM");
        asset_.humanoidSettings.upperArmTwist = FloatValue(Member(humanoid, "upperArmTwist"), 0.5F);
        asset_.humanoidSettings.lowerArmTwist = FloatValue(Member(humanoid, "lowerArmTwist"), 0.5F);
        asset_.humanoidSettings.upperLegTwist = FloatValue(Member(humanoid, "upperLegTwist"), 0.5F);
        asset_.humanoidSettings.lowerLegTwist = FloatValue(Member(humanoid, "lowerLegTwist"), 0.5F);
        asset_.humanoidSettings.armStretch = FloatValue(Member(humanoid, "armStretch"), 0.05F);
        asset_.humanoidSettings.legStretch = FloatValue(Member(humanoid, "legStretch"), 0.05F);
        asset_.humanoidSettings.feetSpacing = FloatValue(Member(humanoid, "feetSpacing"));
        asset_.humanoidSettings.hasTranslationDoF = BoolValue(Member(humanoid, "hasTranslationDoF"));
        const auto& bones = RequireArray(humanoid, "humanBones", "VRM.humanoid");
        for (const auto& bone : bones.GetArray()) {
            const auto name = StringValue(Member(bone, "bone"));
            const auto node = OptionalIndex(bone, "node", "VRM humanBone");
            if (name.empty() || !node) continue;
            if (*node >= asset_.nodes.size()) throw ParseFailure("VRM humanoid bone '" + name + "' has an invalid node");
            if (!asset_.humanoidBones.emplace(name, *node).second) throw ParseFailure("VRM humanoid maps bone '" + name + "' more than once");
        }
        static constexpr std::array<std::string_view, 17> required{
            "hips", "leftUpperLeg", "leftLowerLeg", "leftFoot", "rightUpperLeg", "rightLowerLeg", "rightFoot",
            "spine", "chest", "neck", "head", "leftUpperArm", "leftLowerArm", "leftHand",
            "rightUpperArm", "rightLowerArm", "rightHand"};
        for (const auto bone : required) if (!asset_.humanoidBones.contains(std::string(bone))) {
            throw ParseFailure("VRM humanoid is missing required bone '" + std::string(bone) + "'");
        }

        if (const auto* firstPerson = Member(vrm, "firstPerson")) {
            asset_.firstPerson.bone = OptionalVrmIndex(*firstPerson, "firstPersonBone", "VRM.firstPerson");
            asset_.firstPerson.boneOffset = JsonFloat3(Member(*firstPerson, "firstPersonBoneOffset"));
            if (asset_.firstPerson.bone && *asset_.firstPerson.bone >= asset_.nodes.size()) throw ParseFailure("first-person bone is out of range");
            if (const auto* annotations = Member(*firstPerson, "meshAnnotations")) {
                if (!annotations->IsArray()) throw ParseFailure("VRM.firstPerson.meshAnnotations must be an array");
                for (const auto& annotation : annotations->GetArray()) {
                    FirstPerson::MeshAnnotation parsed{};
                    parsed.mesh = CheckedIndex(
                        RequireMember(annotation, "mesh", "first-person mesh annotation"),
                        "first-person mesh annotation mesh");
                    if (parsed.mesh >= asset_.meshes.size()) {
                        throw ParseFailure("first-person mesh annotation is out of range");
                    }
                    parsed.firstPersonFlag = StringValue(Member(annotation, "firstPersonFlag"), "Auto");
                    asset_.firstPerson.meshAnnotations.push_back(std::move(parsed));
                }
            }
        }
        if (const auto* master = Member(vrm, "blendShapeMaster")) {
            if (const auto* groups = Member(*master, "blendShapeGroups")) {
                if (!groups->IsArray()) throw ParseFailure("blendShapeGroups must be an array");
                for (const auto& source : groups->GetArray()) {
                    BlendShapeGroup group{};
                    group.name = StringValue(Member(source, "name"));
                    group.presetName = StringValue(Member(source, "presetName"), "unknown");
                    group.isBinary = BoolValue(Member(source, "isBinary"));
                    if (const auto* binds = Member(source, "binds")) {
                        if (!binds->IsArray()) throw ParseFailure("blend-shape binds must be an array");
                        for (const auto& bind : binds->GetArray()) {
                            BlendShapeBind parsed{};
                            parsed.mesh = CheckedIndex(RequireMember(bind, "mesh", "blend-shape bind"), "blend-shape mesh");
                            parsed.target = CheckedIndex(RequireMember(bind, "index", "blend-shape bind"), "blend-shape target");
                            parsed.weight = FloatValue(Member(bind, "weight"));
                            if (parsed.mesh >= asset_.meshes.size()) throw ParseFailure("blend-shape bind mesh is out of range");
                            for (const auto& primitive : asset_.meshes[parsed.mesh].primitives) {
                                if (parsed.target >= primitive.morphTargets.size()) throw ParseFailure("blend-shape bind target is out of range");
                            }
                            group.binds.push_back(parsed);
                        }
                    }
                    if (const auto* values = Member(source, "materialValues")) {
                        if (!values->IsArray()) throw ParseFailure("blend-shape materialValues must be an array");
                        for (const auto& value : values->GetArray()) {
                            MaterialValueBind parsed{};
                            parsed.materialName = StringValue(Member(value, "materialName"));
                            parsed.propertyName = StringValue(Member(value, "propertyName"));
                            parsed.targetValue = JsonFloat4(Member(value, "targetValue"));
                            group.materialValues.push_back(std::move(parsed));
                        }
                    }
                    asset_.blendShapeGroups.push_back(std::move(group));
                }
            }
        }
        if (const auto* secondary = Member(vrm, "secondaryAnimation")) ParseSpringBones(*secondary);
    }

    void ParseSpringBones(const rapidjson::Value& secondary) {
        if (const auto* groups = Member(secondary, "colliderGroups")) {
            if (!groups->IsArray()) throw ParseFailure("secondaryAnimation.colliderGroups must be an array");
            for (const auto& source : groups->GetArray()) {
                SpringColliderGroup group{};
                group.node = CheckedIndex(RequireMember(source, "node", "spring collider group"), "spring collider node");
                if (group.node >= asset_.nodes.size()) throw ParseFailure("spring collider node is out of range");
                if (const auto* colliders = Member(source, "colliders")) {
                    if (!colliders->IsArray()) throw ParseFailure("spring colliders must be an array");
                    for (const auto& collider : colliders->GetArray()) {
                        group.colliders.push_back({JsonFloat3(Member(collider, "offset")), FloatValue(Member(collider, "radius"))});
                    }
                }
                asset_.springColliderGroups.push_back(std::move(group));
            }
        }
        if (const auto* groups = Member(secondary, "boneGroups")) {
            if (!groups->IsArray()) throw ParseFailure("secondaryAnimation.boneGroups must be an array");
            for (const auto& source : groups->GetArray()) {
                SpringBoneGroup group{};
                group.comment = StringValue(Member(source, "comment"));
                group.stiffness = FloatValue(Member(source, "stiffiness"), FloatValue(Member(source, "stiffness"), 1.0F));
                group.gravityPower = FloatValue(Member(source, "gravityPower"));
                group.gravityDirection = JsonFloat3(Member(source, "gravityDir"), {0, -1, 0});
                group.dragForce = FloatValue(Member(source, "dragForce"), 0.4F);
                group.hitRadius = FloatValue(Member(source, "hitRadius"));
                group.center = OptionalVrmIndex(source, "center", "spring bone group");
                if (group.center && *group.center >= asset_.nodes.size()) throw ParseFailure("spring center node is out of range");
                if (const auto* bones = Member(source, "bones")) {
                    if (!bones->IsArray()) throw ParseFailure("spring bones must be an array");
                    for (const auto& bone : bones->GetArray()) {
                        const auto index = CheckedIndex(bone, "spring bone");
                        if (index >= asset_.nodes.size()) throw ParseFailure("spring bone node is out of range");
                        group.roots.push_back(index);
                    }
                }
                if (const auto* colliders = Member(source, "colliderGroups")) {
                    if (!colliders->IsArray()) throw ParseFailure("spring collider group references must be an array");
                    for (const auto& collider : colliders->GetArray()) {
                        const auto index = CheckedIndex(collider, "spring collider group");
                        if (index >= asset_.springColliderGroups.size()) throw ParseFailure("spring collider group reference is out of range");
                        group.colliderGroups.push_back(index);
                    }
                }
                asset_.springBoneGroups.push_back(std::move(group));
            }
        }
    }

    void ValidateCrossReferences() const {
        for (std::size_t nodeIndex = 0; nodeIndex < asset_.nodes.size(); ++nodeIndex) {
            const auto& node = asset_.nodes[nodeIndex];
            if (node.mesh && *node.mesh >= asset_.meshes.size()) throw ParseFailure("node mesh reference is out of range");
            if (node.skin && *node.skin >= asset_.skins.size()) throw ParseFailure("node skin reference is out of range");
            if (node.skin && !node.mesh) throw ParseFailure("node has a skin but no mesh");
            if (!node.mesh || !node.skin) continue;
            const auto jointCount = asset_.skins[*node.skin].joints.size();
            for (const auto& primitive : asset_.meshes[*node.mesh].primitives) {
                for (const auto& joints : primitive.joints0) {
                    if (joints.x >= jointCount || joints.y >= jointCount || joints.z >= jointCount || joints.w >= jointCount) {
                        throw ParseFailure("skinned primitive contains a joint index outside its skin");
                    }
                }
            }
        }
        for (const auto& skin : asset_.skins) {
            if (skin.skeleton && *skin.skeleton >= asset_.nodes.size()) throw ParseFailure("skin skeleton reference is out of range");
            for (const auto joint : skin.joints) if (joint >= asset_.nodes.size()) throw ParseFailure("skin joint reference is out of range");
        }
    }

    std::span<const std::uint8_t> file_;
    const AssetLimits& limits_;
    std::span<const std::uint8_t> binary_;
    rapidjson::Document document_;
    std::vector<BufferView> bufferViews_;
    std::vector<Accessor> accessors_;
    VrmAsset asset_;
    std::vector<std::string> warnings_;
};

} // namespace

ParseResult ParseVrm0Bytes(std::span<const std::uint8_t> bytes, std::string sourceLabel, const AssetLimits& limits) {
    try {
        return Parser(bytes, std::move(sourceLabel), limits).Run();
    } catch (const ParseFailure& failure) {
        return {std::nullopt, failure.what(), {}};
    } catch (const std::bad_alloc&) {
        return {std::nullopt, "VRM load exceeded available memory", {}};
    } catch (const std::exception& failure) {
        return {std::nullopt, std::string("unexpected VRM parser failure: ") + failure.what(), {}};
    }
}

ParseResult ParseVrm0File(const std::filesystem::path& path, const AssetLimits& limits) {
    try {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) return {std::nullopt, "could not open VRM file: " + path.string(), {}};
        const auto end = stream.tellg();
        if (end < 0) return {std::nullopt, "could not determine VRM file length", {}};
        const auto length = static_cast<std::uint64_t>(end);
        if (length > limits.maximumFileBytes) return {std::nullopt, "file exceeds configured byte limit", {}};
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
        stream.seekg(0);
        if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            return {std::nullopt, "could not read complete VRM file", {}};
        }
        return ParseVrm0Bytes(bytes, path.string(), limits);
    } catch (const std::bad_alloc&) {
        return {std::nullopt, "VRM file buffer exceeded available memory", {}};
    } catch (const std::exception& failure) {
        return {std::nullopt, std::string("VRM file read failed: ") + failure.what(), {}};
    }
}

} // namespace saberstage::avatar::vrm
