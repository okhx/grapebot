
#include "gltf.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace grape::gltf;

static int g_failures = 0;

static void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

namespace {

void appendBytes(std::vector<unsigned char>& out, const void* data,
                 std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void appendWord(std::vector<unsigned char>& out, std::uint32_t value) {
    appendBytes(out, &value, sizeof(value));
}

std::vector<unsigned char> container(const std::string& json,
                                     const std::vector<unsigned char>& binary) {
    std::string paddedJson = json;
    while (paddedJson.size() % 4) paddedJson.push_back(' ');
    std::vector<unsigned char> paddedBinary = binary;
    while (paddedBinary.size() % 4) paddedBinary.push_back(0);

    std::vector<unsigned char> out;
    appendBytes(out, "glTF", 4);
    appendWord(out, 2);
    appendWord(out, static_cast<std::uint32_t>(
        12 + 8 + paddedJson.size() + 8 + paddedBinary.size()));
    appendWord(out, static_cast<std::uint32_t>(paddedJson.size()));
    appendWord(out, 0x4E4F534A);
    appendBytes(out, paddedJson.data(), paddedJson.size());
    appendWord(out, static_cast<std::uint32_t>(paddedBinary.size()));
    appendWord(out, 0x004E4942);
    appendBytes(out, paddedBinary.data(), paddedBinary.size());
    return out;
}

struct CubeData {
    std::vector<unsigned char> m_bytes;
    std::size_t m_positionOffset = 0;
    std::size_t m_normalOffset = 0;
    std::size_t m_indexOffset = 0;
    std::size_t m_vertexCount = 0;
    std::size_t m_indexCount = 0;
};

CubeData cube(float size) {
    const float h = size * .5f;
    const float positions[8][3] = {
        {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
        {-h, -h, h},  {h, -h, h},  {h, h, h},  {-h, h, h},
    };
    const std::uint16_t indices[36] = {
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
        3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2,
    };

    CubeData data;
    data.m_vertexCount = 8;
    data.m_indexCount = 36;
    data.m_positionOffset = 0;
    for (const auto& point : positions)
        appendBytes(data.m_bytes, point, sizeof(point));
    data.m_normalOffset = data.m_bytes.size();
    for (const auto& point : positions) {
        const float normal[3] = {point[0] / h, point[1] / h, point[2] / h};
        appendBytes(data.m_bytes, normal, sizeof(normal));
    }
    data.m_indexOffset = data.m_bytes.size();
    appendBytes(data.m_bytes, indices, sizeof(indices));
    return data;
}

std::string cubeJson(const CubeData& data, const std::string& nodeExtra) {
    char buffer[2048];
    std::snprintf(buffer, sizeof(buffer), R"({
"asset":{"version":"2.0"},
"scene":0,
"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0%s}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2}]}],
"accessors":[
 {"bufferView":0,"componentType":5126,"count":%zu,"type":"VEC3"},
 {"bufferView":1,"componentType":5126,"count":%zu,"type":"VEC3"},
 {"bufferView":2,"componentType":5123,"count":%zu,"type":"SCALAR"}],
"bufferViews":[
 {"buffer":0,"byteOffset":%zu,"byteLength":%zu},
 {"buffer":0,"byteOffset":%zu,"byteLength":%zu},
 {"buffer":0,"byteOffset":%zu,"byteLength":%zu}],
"buffers":[{"byteLength":%zu}]
})",
        nodeExtra.c_str(), data.m_vertexCount, data.m_vertexCount,
        data.m_indexCount,
        data.m_positionOffset, data.m_vertexCount * 12,
        data.m_normalOffset, data.m_vertexCount * 12,
        data.m_indexOffset, data.m_indexCount * 2,
        data.m_bytes.size());
    return buffer;
}

}

int main() {
    const CubeData data = cube(2.0f);

    {
        const auto file = container(cubeJson(data, ""), data.m_bytes);
        const Result result = load(file);
        check(result.ok(), "a plain container loads: " + result.m_error);
        check(result.m_mesh.triangles() == 12, "and has twelve triangles");
        check(result.m_mesh.m_positions.size() == 8, "and eight vertices");
        check(result.m_mesh.m_normals.size() == 8, "with a normal each");
        check(std::fabs(result.m_mesh.m_max.x - 1.0f) < .001f &&
                  std::fabs(result.m_mesh.m_min.x + 1.0f) < .001f,
              "the original bounds are kept");
        check(std::fabs(result.m_mesh.m_unit - 2.0f) < .001f,
              "and the unit it was normalised by");
        for (const Vec3& point : result.m_mesh.m_positions)
            check(std::fabs(point.x) <= .5001f && std::fabs(point.y) <= .5001f &&
                      std::fabs(point.z) <= .5001f,
                  "every vertex lands inside the unit cube");
    }

    {
        const auto file = container(
            cubeJson(data, R"(,"translation":[100,0,0],"scale":[1,3,1])"),
            data.m_bytes);
        const Result result = load(file);
        check(result.ok(), "a transformed node loads: " + result.m_error);
        check(std::fabs(result.m_mesh.m_min.x - 99.0f) < .001f,
              "translation is applied");
        check(std::fabs(result.m_mesh.m_max.y - 3.0f) < .001f,
              "and so is scale");
        check(std::fabs(result.m_mesh.m_unit - 6.0f) < .001f,
              "the longest axis sets the unit");
    }

    {
        const auto file = container(
            cubeJson(data, R"(,"rotation":[0,0,0.70710678,0.70710678])"),
            data.m_bytes);
        const Result result = load(file);
        check(result.ok(), "a rotated node loads: " + result.m_error);
        check(std::fabs(result.m_mesh.m_max.y - 1.0f) < .01f &&
                  std::fabs(result.m_mesh.m_min.y + 1.0f) < .01f,
              "a quaternion rotation is applied");
    }

    {
        static const char* alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        for (std::size_t i = 0; i < data.m_bytes.size(); i += 3) {
            const unsigned a = data.m_bytes[i];
            const unsigned b = i + 1 < data.m_bytes.size() ? data.m_bytes[i + 1] : 0;
            const unsigned c = i + 2 < data.m_bytes.size() ? data.m_bytes[i + 2] : 0;
            const unsigned triple = (a << 16) | (b << 8) | c;
            encoded += alphabet[(triple >> 18) & 63];
            encoded += alphabet[(triple >> 12) & 63];
            encoded += i + 1 < data.m_bytes.size() ? alphabet[(triple >> 6) & 63] : '=';
            encoded += i + 2 < data.m_bytes.size() ? alphabet[triple & 63] : '=';
        }
        std::string json = cubeJson(data, "");
        const std::string uri =
            ",\"uri\":\"data:application/octet-stream;base64," + encoded + "\"";
        const auto at = json.find("\"buffers\":[{");
        json.insert(at + std::strlen("\"buffers\":[{"), uri.substr(1) + ",");
        const std::vector<unsigned char> bytes(json.begin(), json.end());
        const Result result = load(bytes);
        check(result.ok(), "a base64 buffer loads: " + result.m_error);
        check(result.m_mesh.triangles() == 12, "with the same twelve triangles");
    }

    {
        std::string json = cubeJson(data, "");
        const auto at = json.find("\"buffers\":[{");
        json.insert(at + std::strlen("\"buffers\":[{"), "\"uri\":\"cube.bin\",");
        const std::vector<unsigned char> bytes(json.begin(), json.end());

        const Result refused = load(bytes);
        check(!refused.ok(),
              "an external buffer with no resolver is refused");

        bool asked = false;
        const Result resolved = load(bytes, [&](std::string_view uri) {
            asked = uri == "cube.bin";
            return data.m_bytes;
        });
        check(asked, "the resolver is asked for the uri as written");
        check(resolved.ok(), "and the model loads: " + resolved.m_error);
        check(resolved.m_mesh.triangles() == 12, "with its triangles");

        const Result missing = load(bytes, [](std::string_view) {
            return std::vector<unsigned char>{};
        });
        check(!missing.ok(), "a missing external buffer is an error");
    }

    {
        std::string json = cubeJson(data, "");
        const auto at = json.find("\"byteLength\":" +
                                  std::to_string(data.m_indexCount * 2));
        json.replace(at, std::strlen("\"byteLength\":72"),
                     "\"byteLength\":99999");
        const std::vector<unsigned char> bytes(json.begin(), json.end());
        const Result result = load(container(json, data.m_bytes));
        check(!result.ok(), "a bufferView past the end of its buffer is refused");
    }
    {
        std::string json = cubeJson(data, "");
        const auto at = json.find("\"count\":8,\"type\":\"VEC3\"");
        json.replace(at, std::strlen("\"count\":8"), "\"count\":2");
        const Result result = load(container(json, data.m_bytes));
        check(!result.ok(), "an out-of-range index is refused");
    }

    {
        check(!load({}).ok(), "an empty file is refused");
        const std::string junk = "not json at all";
        check(!load(std::vector<unsigned char>(junk.begin(), junk.end())).ok(),
              "a text file is refused");
        const std::string empty = R"({"asset":{"version":"2.0"}})";
        check(!load(std::vector<unsigned char>(empty.begin(), empty.end())).ok(),
              "a document with no meshes is refused");
    }

    {
        CubeData data = cube(2.0f);
        const std::size_t colorOffset = data.m_bytes.size();
        for (int i = 0; i < 8; ++i) {
            const unsigned char rgba[4] = {
                static_cast<unsigned char>(i * 32), 128, 255, 255};
            appendBytes(data.m_bytes, rgba, sizeof(rgba));
        }
        const std::size_t uvOffset = data.m_bytes.size();
        for (int i = 0; i < 8; ++i) {
            const float uv[2] = {i / 8.0f, 1.0f - i / 8.0f};
            appendBytes(data.m_bytes, uv, sizeof(uv));
        }
        const std::size_t imageOffset = data.m_bytes.size();
        const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a,
                                     0x0a, 'f', 'a', 'k', 'e'};
        appendBytes(data.m_bytes, png, sizeof(png));

        char buffer[3072];
        std::snprintf(buffer, sizeof(buffer), R"({
"asset":{"version":"2.0"},
"scene":0,
"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0}],
"meshes":[{"primitives":[
 {"attributes":{"POSITION":0,"NORMAL":1,"COLOR_0":3,"TEXCOORD_0":4},"indices":2,"material":0}]}],
"materials":[
 {"pbrMetallicRoughness":{"baseColorFactor":[0.25,0.5,0.75,1.0],
                          "baseColorTexture":{"index":0}},"doubleSided":true}],
"textures":[{"source":0}],
"images":[{"bufferView":5,"mimeType":"image/png"}],
"accessors":[
 {"bufferView":0,"componentType":5126,"count":8,"type":"VEC3"},
 {"bufferView":1,"componentType":5126,"count":8,"type":"VEC3"},
 {"bufferView":2,"componentType":5123,"count":36,"type":"SCALAR"},
 {"bufferView":3,"componentType":5121,"normalized":true,"count":8,"type":"VEC4"},
 {"bufferView":4,"componentType":5126,"count":8,"type":"VEC2"}],
"bufferViews":[
 {"buffer":0,"byteOffset":%zu,"byteLength":96},
 {"buffer":0,"byteOffset":%zu,"byteLength":96},
 {"buffer":0,"byteOffset":%zu,"byteLength":72},
 {"buffer":0,"byteOffset":%zu,"byteLength":32},
 {"buffer":0,"byteOffset":%zu,"byteLength":64},
 {"buffer":0,"byteOffset":%zu,"byteLength":12}],
"buffers":[{"byteLength":%zu}]
})",
            data.m_positionOffset, data.m_normalOffset, data.m_indexOffset,
            colorOffset, uvOffset, imageOffset, data.m_bytes.size());

        const Result result = load(container(buffer, data.m_bytes));
        check(result.ok(), "a textured model loads: " + result.m_error);
        const Mesh& mesh = result.m_mesh;

        check(mesh.m_materials.size() == 1, "its material is read");
        check(std::fabs(mesh.m_materials[0].m_baseColor[0] -
                        linearToSrgb(0.25f)) < .001f &&
                  std::fabs(mesh.m_materials[0].m_baseColor[2] -
                            linearToSrgb(0.75f)) < .001f,
              "with its base colour factor, converted to sRGB");
        check(mesh.m_materials[0].m_baseColor[0] > 0.25f,
              "which is brighter than the linear value it was stored as");
        check(std::fabs(mesh.m_materials[0].m_baseColor[3] - 1.0f) < .001f,
              "and alpha left alone");
        check(mesh.m_parts.size() == 1 && mesh.m_parts[0].m_material == 0,
              "and the triangles point at it");
        check(mesh.m_parts[0].m_count == 36, "covering every index");

        check(mesh.m_hasColors, "vertex colours are found");
        check(mesh.m_colors.size() == 8 * 4, "four channels for every vertex");
        check(std::fabs(mesh.m_colors[4 * 1 + 0] -
                        linearToSrgb(32.0f / 255.0f)) < .005f,
              "and normalised bytes are scaled back to 0-1, then to sRGB");
        check(std::fabs(mesh.m_colors[3] - 1.0f) < .001f, "alpha included");

        check(mesh.m_hasUvs, "texture coordinates are found");
        check(mesh.m_uvs.size() == 8 * 2, "two per vertex");
        check(std::fabs(mesh.m_uvs[2] - 0.125f) < .001f, "and read correctly");

        check(mesh.m_images.size() == 1, "the image is found");
        check(mesh.m_images[0].m_bytes.size() == sizeof(png),
              "with its bytes intact");
        check(mesh.m_images[0].m_bytes[1] == 'P', "and not mangled");
        check(mesh.m_materials[0].m_texture == 0,
              "and the material resolves through textures[] to it");
    }

    {
        const CubeData data = cube(2.0f);
        const Result result = load(container(cubeJson(data, ""), data.m_bytes));
        check(result.ok(), "a plain model still loads");
        check(!result.m_mesh.m_hasColors && !result.m_mesh.m_hasUvs,
              "and reports that it has neither");
        check(result.m_mesh.m_colors.size() ==
                  result.m_mesh.m_positions.size() * 4,
              "but is padded to full length anyway");
        check(result.m_mesh.m_uvs.size() ==
                  result.m_mesh.m_positions.size() * 2,
              "including the texture coordinates");
        check(result.m_mesh.m_parts.size() == 1 &&
                  result.m_mesh.m_parts[0].m_material == -1,
              "and its triangles carry no material");
        check(result.m_mesh.materialFor(result.m_mesh.m_parts[0]) == nullptr,
              "which resolves to nothing rather than out of bounds");
    }

    {
        check(std::fabs(linearToSrgb(0.0f)) < .0001f, "black stays black");
        check(std::fabs(linearToSrgb(1.0f) - 1.0f) < .0001f,
              "and white stays white");
        check(linearToSrgb(0.05f) > 0.2f,
              "a dark linear value is not nearly black in sRGB");
        check(std::fabs(linearToSrgb(0.5f) - 0.7354f) < .001f,
              "and mid grey lands where sRGB says it should");
        check(linearToSrgb(-1.0f) == 0.0f &&
                  std::fabs(linearToSrgb(2.0f) - 1.0f) < .0001f,
              "values outside the range are clamped rather than wrapped");
    }

    {
        CubeData data = cube(2.0f);
        const std::size_t imageOffset = data.m_bytes.size();
        const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a,
                                     0x0a, 'f', 'a', 'k', 'e'};
        appendBytes(data.m_bytes, png, sizeof(png));

        char buffer[2048];
        std::snprintf(buffer, sizeof(buffer), R"({
"asset":{"version":"2.0"},
"scene":0,
"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0}],
"meshes":[{"primitives":[
 {"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0}]}],
"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
"textures":[{"source":0}],
"images":[{"bufferView":3,"mimeType":"image/png"}],
"accessors":[
 {"bufferView":0,"componentType":5126,"count":8,"type":"VEC3"},
 {"bufferView":1,"componentType":5126,"count":8,"type":"VEC3"},
 {"bufferView":2,"componentType":5123,"count":36,"type":"SCALAR"}],
"bufferViews":[
 {"buffer":0,"byteOffset":%zu,"byteLength":96},
 {"buffer":0,"byteOffset":%zu,"byteLength":96},
 {"buffer":0,"byteOffset":%zu,"byteLength":72},
 {"buffer":0,"byteOffset":%zu,"byteLength":12}],
"buffers":[{"byteLength":%zu}]
})",
            data.m_positionOffset, data.m_normalOffset, data.m_indexOffset,
            imageOffset, data.m_bytes.size());

        const Result result = load(container(buffer, data.m_bytes));
        check(result.ok(), "a model with a texture but no UVs still loads");
        check(!result.m_mesh.m_hasUvs, "and reports that it has no UVs");
        check(result.m_mesh.m_materials.size() == 1 &&
                  result.m_mesh.m_materials[0].m_texture == -1,
              "and its texture is declined rather than sampled at one texel");
    }

    if (g_failures) {
        std::printf("\ngltf: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("\ngltf: all checks passed\n");
    return 0;
}
